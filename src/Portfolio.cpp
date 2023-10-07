#include "pch.h"
#include <cmath>
#include <tbb/parallel_for_each.h>
#include <algorithm>
#include "Portfolio.h"
#include "AgisStrategy.h"

import Asset;

using namespace Agis;
using namespace rapidjson;

std::atomic<size_t> Position::position_counter(0);
std::atomic<size_t> Portfolio::portfolio_counter(0);


//============================================================================
Position::Position(
    AgisStrategy* strategy,
    OrderPtr const& filled_order_
) :
    __asset(filled_order_->__asset)
{
    //populate common position values
    this->broker_index = filled_order_->get_broker_index();
    this->asset_index = filled_order_->get_asset_index();
    this->portfolio_id = filled_order_->get_portfolio_index();
    this->units = filled_order_->get_units();

    // populate order values
    this->nlv = filled_order_->get_units() * filled_order_->get_average_price();
    this->average_price = filled_order_->get_average_price();
    this->last_price = filled_order_->get_average_price();
    this->position_open_time = filled_order_->get_fill_time();

    // insert the new trade
    auto trade = std::make_shared<Trade>(
        strategy,
        filled_order_
    );

    this->trades.insert({trade->strategy_index,trade});
    strategy->__add_trade(trade);
    this->position_id = position_counter++;
}


//============================================================================
std::optional<TradeRef> Position::__get_trade(size_t strategy_index) const
{
    if (this->trades.contains(strategy_index))
    {
        return std::cref(this->trades.at(strategy_index));
    }
    else
    {
        return std::nullopt;
    }
}


//============================================================================
void Position::__evaluate(ThreadSafeVector<OrderPtr>& orders, bool on_close, bool is_reprice)
{
    this->last_price = this->__asset->__get_market_price(on_close);
    if (this->last_price == 0.0f) return;
    this->nlv = 0;
    this->unrealized_pl = this->units*(this->last_price-this->average_price);
    if (on_close && !is_reprice) { this->bars_held++; }

    for (auto& trade_pair : this->trades) 
    {
        auto& trade = trade_pair.second;
        trade->evaluate(this->last_price, on_close, is_reprice);
        this->nlv += trade->nlv;

        // test trade exit
        if (!trade->exit.has_value()) { continue; }
        if (trade->exit.value()->exit())
        {
            auto order = trade->generate_trade_inverse();
            // set the state to Cheat to allow for the order to be filled and then processed by
            // the portfolio in a single call to order router
            order->__set_state(OrderState::CHEAT);
            orders.push_back(std::move(order));

            // if trade exit has additional child order send that as well. It is a raw poitner so
            // take ownership of it.
            if (trade->exit.value()->has_child_order()) {
                auto child_order = trade->exit.value()->take_child_order();
                auto child_order_ptr = std::unique_ptr<Order>(std::move(child_order));
                child_order_ptr->__set_state(OrderState::CHEAT);
				orders.push_back(std::move(child_order_ptr));
            }
        }
    }
}


//============================================================================
void Position::close(OrderPtr const& order, std::vector<SharedTradePtr>& trade_history)
{
    // close the position
    auto price = order->get_average_price();
    this->close_price = price;
    this->position_close_time = order->get_fill_time();
    this->realized_pl += this->units*(price-this->average_price);
    this->unrealized_pl = 0;

    for (auto& element : trades) {
        // Retrieve the trade pointer
        SharedTradePtr trade = element.second;
        trade->close(order);
        trade->strategy->__remove_trade(trade->asset_index);

        // Add the unique pointer to the vector
        trade_history.push_back(trade);
    }
}


//============================================================================
void Position::adjust(AgisStrategy* strategy, OrderPtr const& order, std::vector<SharedTradePtr>& trade_history)
{
    auto units_ = order->get_units();
    auto fill_price = order->get_average_price();

    // increasing position
    if (units_ * this->units > 0)
    {
        double new_units = abs(this->units) + abs(units_);
        this->average_price = (
            (abs(this->units)*this->average_price) +
            (abs(units_)*fill_price)
        );
        this->average_price /= new_units;

    }
    // reducing position
    else
    {
        this->realized_pl += (abs(units_) * (fill_price - this->average_price));
    }

    // adjust position units
    this->units += units_;

    //test to see if strategy already has a trade
    auto strategy_id = order->get_strategy_index();
    auto trade_opt = this->__get_trade(strategy_id);
    if (!trade_opt.has_value())
    {
        auto trade = std::make_shared<Trade>(
            strategy,
            order
        );
        this->trades.insert({ strategy_id,trade });
        strategy->__add_trade(trade);
    }
    else
    {
        auto& trade_ptr = trade_opt->get();
        if (abs(trade_ptr->units + units_) < 1e-10)
        {
            trade_ptr->close(order);
            SharedTradePtr extracted_trade = this->trades.at(strategy_id);
            this->trades.erase(strategy_id);
            strategy->__remove_trade(order->get_asset_index());
            trade_history.push_back(extracted_trade);
        }
        else
        {
            // test to see if the trade is being reversed
            if(std::signbit(units_) != std::signbit(trade_ptr->units) 
                && abs(units_) > abs(trade_ptr->units))
			{
                // get the number of units left over after reversing the trade
                auto units_left = trade_ptr->units + units_;

				trade_ptr->close(order);
                SharedTradePtr extracted_trade = this->trades.at(strategy_id);
				this->trades.erase(strategy_id);
                strategy->__remove_trade(order->get_asset_index());
				trade_history.push_back(extracted_trade);

                // open a new trade with the new order minus the units needed to close out 
                // the previous trade
                order->set_units(units_left);
                auto trade = std::make_shared<Trade>(
                    strategy,
                    order
                );
                this->trades.insert({ strategy_id,trade });
                strategy->__add_trade(trade);
                order->set_units(units_);
			}
            else
            {
                trade_ptr->adjust(order);
            }
        }
    }
}


//============================================================================
OrderPtr Position::generate_position_inverse()
{   
    return std::make_unique<Order>(
        OrderType::MARKET_ORDER,
        this->asset_index,
        -1 * this->units,
        DEFAULT_STRAT_ID,
        this->portfolio_id,
        this->broker_index
    );
}


//============================================================================
void PortfolioMap::__evaluate(bool on_close, bool is_reprice)
{
    // Define a lambda function that calls next for each strategy
    auto portfolio_evaluate = [&](auto& portfolio) {
        AGIS_DO_OR_THROW(portfolio.second->__evaluate(on_close, is_reprice));
    };

    tbb::parallel_for_each(
        this->portfolios.begin(),
        this->portfolios.end(),
        portfolio_evaluate
    );
}

void PortfolioMap::__clear()
{
    Portfolio::__reset_counter();
    AgisStrategy::__reset_counter();
    this->portfolios.clear();
    this->portfolio_map.clear();
}


//============================================================================
void PortfolioMap::__reset()
{
    for(auto& portfolio_pair : this->portfolios)
    {
        auto& portfolio = portfolio_pair.second;
        portfolio->__reset();
    }
}


//============================================================================
void PortfolioMap::__build(size_t size)
{
    for (auto& portfolio_pair : this->portfolios)
    {
        auto& portfolio = portfolio_pair.second;
        portfolio->tracers.build<Portfolio>(portfolio.get(), size);
    }
}


//============================================================================
void PortfolioMap::__on_order_fill(OrderPtr const& order)
{
    auto& portfolio = this->portfolios[order->get_portfolio_index()];
    portfolio->__on_order_fill(order);
}


//============================================================================
void PortfolioMap::__remember_order(SharedOrderPtr order)
{
    auto& portfolio = this->portfolios.at(order.get()->get_portfolio_index());
    portfolio->__remember_order(order);
}


//============================================================================
void PortfolioMap::__on_assets_expired(AgisRouter& router, ThreadSafeVector<size_t> const& ids)
{
    if (!ids.size()) return;
    for (auto& portfolio_pair : this->portfolios)
    {
        auto& portfolio = portfolio_pair.second;
        portfolio->__on_assets_expired(router, ids);
    }
}


//============================================================================
void PortfolioMap::__register_portfolio(PortfolioPtr portfolio)
{
    this->portfolio_map.emplace(portfolio->__get_portfolio_id(), portfolio->__get_index());
    this->portfolios.emplace(portfolio->__get_index(), std::move(portfolio));
}


//============================================================================
void PortfolioMap::__remove_portfolio(std::string const& portfolio_id)
{
    auto index = this->portfolio_map.at(portfolio_id);
    this->portfolios.erase(index);
    this->portfolio_map.erase(portfolio_id);
}


//============================================================================
void PortfolioMap::__remove_strategy(size_t strategy_index)
{
    for (auto& p : this->portfolios)
    {
        if (p.second->__strategy_exists(strategy_index))
        {
            p.second->__remove_strategy(strategy_index);
        }
    }
}


//============================================================================
AgisResult<std::string> PortfolioMap::__get_portfolio_id(size_t const& index) const
{
    // find the strategy with the given index and return it's id
    auto portfolio = std::find_if(
        this->portfolio_map.begin(),
        this->portfolio_map.end(),
        [index](auto& portfolio) {
            return portfolio.second == index;
        }
    );
    // test to see if portfolio was found
    if (portfolio == this->portfolio_map.end())
    {
            return AgisResult<std::string>(AGIS_EXCEP("failed to find strategy"));
    }
    // return the strategy id
    return AgisResult<std::string>(portfolio->first);
}

//============================================================================
void PortfolioMap::__register_strategy(MAgisStrategyRef strategy)
{
    auto& portfolio = this->portfolios.at(strategy.get()->get_portfolio_index());
    portfolio->register_strategy(strategy);
}


//============================================================================
void PortfolioMap::__reload_strategies(AgisStrategyMap* strategies)
{
    // loop over portfolios and clear their mappings
    for (auto& portfolio_pair : this->portfolios)
    {
		auto& portfolio = portfolio_pair.second;
		portfolio->strategies.clear();
        portfolio->strategy_ids.clear();
	}
    // re-register all strategies
    for (auto& strategy : strategies->__get_strategies_mut())
    {
        this->__register_strategy(strategy.second);
	}
}


//============================================================================
PortfolioPtr const PortfolioMap::__get_portfolio(std::string const& id) const
{
    auto portfolio_index = this->portfolio_map.at(id);
    return this->portfolios.at(portfolio_index);
}


//============================================================================
std::expected<rapidjson::Document, AgisException> Portfolio::to_json() const
{
    Document j(kObjectType);
    j.AddMember("starting_cash", this->tracers.starting_cash.load(), j.GetAllocator());

    Value strategyArray(kArrayType);
    for (const auto& strategyPair : strategies) {
        const auto& strategy = strategyPair.second;
        auto strategy_json = strategy->to_json(); // Serialize the Strategy to RapidJSON
        
        if (!strategy_json.has_value()) {
            return std::unexpected<AgisException>{strategy_json.error()};
        }
        
        strategyArray.PushBack(strategy_json.value(), j.GetAllocator());
    }

    if (benchmark_strategy) {
        auto strategy_json = benchmark_strategy->to_json(); // Serialize the BenchmarkStrategy to RapidJSON
        if (!strategy_json.has_value()) {
            return std::unexpected<AgisException>{strategy_json.error()};
        }
        strategyArray.PushBack(strategy_json.value(), j.GetAllocator());
    }

    j.AddMember("strategies", strategyArray, j.GetAllocator());

    return j;
}


//============================================================================
void PortfolioMap::restore(AgisRouter& router, const Document& j) {
    Portfolio::__reset_counter();

    const Value& portfolioArray = j["portfolios"];
    if (portfolioArray.IsArray()) {
        for (SizeType i = 0; i < portfolioArray.Size(); i++) {
            const Value& portfolioJson = portfolioArray[i];

            // Extract portfolio_id and starting_cash from the JSON
            const std::string& portfolio_id = portfolioJson["portfolio_id"].GetString();
            double starting_cash = portfolioJson["starting_cash"].GetDouble();

            // Create a new Portfolio object and add it to the map
            auto portfolio = std::make_shared<Portfolio>(router, portfolio_id, starting_cash);
            this->__register_portfolio(std::move(portfolio));
        }
    }
}


//============================================================================
PortfolioRef
PortfolioMap::get_portfolio(std::string const& id) const
{
    auto index = this->portfolio_map.at(id);
    auto p = std::cref(this->portfolios.at(index));
    return p;
}


//============================================================================
std::vector<std::string>
PortfolioMap::get_portfolio_ids() const
{
    std::vector<std::string> v;
    for (auto p : this->portfolio_map)
    {
        v.push_back(p.first);
    }
    return v;
}


//============================================================================
std::expected<rapidjson::Document, AgisException>
PortfolioMap::to_json() const
{
    Document j(kObjectType);
    for (const auto& pair : portfolios) {
        const std::shared_ptr<Portfolio>& portfolio = pair.second;

        Document portfolioJson(kObjectType);
        auto portfolio_json = portfolio->to_json();

        if (!portfolio_json.has_value()) {
            return portfolio_json;
        }

        Value portfolio_id_value(portfolio->__get_portfolio_id().c_str(), j.GetAllocator());
        j.AddMember(portfolio_id_value, portfolio_json.value(), j.GetAllocator());
    }
    return j;
}


//============================================================================
Portfolio::Portfolio(AgisRouter& router_, std::string const & portfolio_id_, double cash_) :
    router(router_),
    tracers(this, cash_)
{
    this->portfolio_id = portfolio_id_;
    this->portfolio_index = portfolio_counter++;
}


//============================================================================
void Portfolio::__on_phantom_order(OrderPtr const& order)
{
    auto trade = benchmark_strategy->get_trade(order->get_asset_index());

    // opening new trade
    if (!trade.has_value())
    {
        // insert the new trade
        auto trade = std::make_shared<Trade>(
            benchmark_strategy,
            order
        );
        benchmark_strategy->__add_trade(trade);
    }
    // modifying existing trade
    else if (trade.value()->units + order->get_units() > 1e-7)
    {
	    trade.value()->adjust(order);
    }
    // closing existing trade
    else
	{
        trade.value()->close(order);
		benchmark_strategy->__remove_trade(order->get_asset_index());
	}
    // adjust cash levels
    auto amount = order->get_units() * order->get_average_price();
    benchmark_strategy->tracers.cash_add_assign(-amount);
}


//============================================================================
void Portfolio::__on_order_fill(OrderPtr const& order)
{
    // aquire write lock on position mutex
    auto asset_index = order->get_asset_index();
    LOCK_GUARD
    // constructs it inside the map if doesn't exist
    std::mutex& position_mutex = this->position_mutexes[asset_index];
    position_mutex.lock();
    UNLOCK_GUARD

    // check if is benchmark order
    if (order->phantom_order)
    {
        this->__on_phantom_order(order);
        position_mutex.unlock();
        return;
    }

    // process new incoming order
    if (!this->position_exists(asset_index))
    {
        this->open_position(order);
    }
    else 
    {
        auto order_units = order->get_units();
        auto& position = this->__get_position(asset_index);
        if (abs(position->units + order_units) > 1e-10)
        {
            this->modify_position(order);
        }
        else if (order->force_close) {
            this->close_position(order);
        }
        else if (
            !position->__trade_exits(order->get_strategy_index()) ||
            position->__get_trade_count() > 1
            )
        {
            this->modify_position(order);
        }
        else this->close_position(order);
    }
    // unlock the position mutex (following opps are atomic)
    position_mutex.unlock();

    auto cash_adjustment = order->get_cash_impact();

    // adjust the strategy's cash as well as the portfolios
    auto strategy = this->strategies.at(order->get_strategy_index());
    strategy->tracers.cash_add_assign(-cash_adjustment);
    this->tracers.cash_add_assign(-cash_adjustment);
}


//============================================================================
AgisResult<bool> Portfolio::__evaluate(bool on_close, bool is_reprice)
{
    LOCK_GUARD
    this->tracers.nlv.store(0);
    this->unrealized_pl = 0;
    ThreadSafeVector<OrderPtr> orders;

    // when evaluating the portfolio we need to zero out strategy levels so they can 
    // be recalculated using their respective trades
    for (auto& strategy : this->strategies)
    {
        strategy.second->zero_out_tracers();
    }
    if (benchmark_strategy) benchmark_strategy->zero_out_tracers();
    

    // evalute all open positions and their respective trades
    for (auto it = this->positions.begin(); it != positions.end(); ++it)
    {        
        // evaluate the indivual positions and allow for any orders that are generated
        // as a result of the new valuation
        auto& position = it->second;

        position->__evaluate(orders, on_close, is_reprice);
        this->tracers.nlv.fetch_add(position->nlv);
        this->unrealized_pl += position->unrealized_pl;

        if (is_reprice) continue;

        // if asset expire next step clear from the portfolio
        if (position->__asset->__is_last_row())
        {
            auto order = position->generate_position_inverse();
            order->__set_state(OrderState::CHEAT);
            order->__set_force_close(true);
            router.place_order(std::move(order));
        }
    }
    this->tracers.nlv.fetch_add(this->tracers.cash.load());


    // and orders placed by the portfolio to clean up or force close
    if (orders.size())
    {
        size_t count = orders.size();
        for (int i = 0; i < count; i++) {
            std::optional<OrderPtr> order = orders.pop_back();
            router.place_order(std::move(order.value()));
        }
    }

    if (is_reprice)
    {
        UNLOCK_GUARD
        return AgisResult<bool>(true);
    }

    // store portfolio stats at the current level
    this->tracers.evaluate();

    // log strategy levels
    for (const auto& strat : this->strategies)
    {
        strat.second->tracers.nlv.fetch_add(strat.second->tracers.cash.load());
        auto res = strat.second->__evaluate(on_close);
        if (res.is_exception()) {
            UNLOCK_GUARD;
            return res;
        }

    }
    if (this->benchmark_strategy)  this->benchmark_strategy->evluate();
    UNLOCK_GUARD
    return AgisResult<bool>(true);
}


//============================================================================
std::optional<PositionRef> Portfolio::get_position(size_t asset_index) const
{
    if (this->positions.contains(asset_index))
    {
        return std::cref(this->positions.at(asset_index));
    }
    else
    {
        return std::nullopt;
    }
}


//============================================================================
std::optional<TradeRef> Portfolio::get_trade(size_t asset_index, std::string const& strategy_id)
{
    if(!this->strategy_ids.contains(strategy_id)) return std::nullopt;
    return this->get_trade(asset_index, this->strategy_ids.at(strategy_id));
}


//============================================================================
std::optional<TradeRef> Portfolio::get_trade(size_t asset_index, size_t strategy_index)
{
    auto position = this->get_position(asset_index);
	if (!position.has_value()) { return std::nullopt; }
	return position.value().get()->__get_trade(strategy_index);
}


//============================================================================
std::vector<size_t> Portfolio::get_strategy_positions(size_t strategy_index) const
{
    std::vector<size_t> v;
    for (const auto& position : this->positions)
    {
        if (position.second->__get_trade(strategy_index).has_value()) {
            v.push_back(position.first);
        }
    }
    return v;
}


//============================================================================
AGIS_API std::vector<std::string> Portfolio::get_strategy_ids() const
{
    std::vector<std::string> v;
    for (auto pair : this->strategy_ids)
    {
        v.push_back(pair.first);
    }
    if(this->benchmark_strategy) v.push_back(this->benchmark_strategy->get_strategy_id());
    return v;
}


//============================================================================
AGIS_API AgisStrategy const* Portfolio::__get_strategy(std::string const& id)
{
    auto strategy_index = this->strategy_ids.at(id);
    return this->strategies.at(strategy_index);
}


//============================================================================
AGIS_API void Portfolio::register_strategy(MAgisStrategyRef strategy)
{
    LOCK_GUARD
    // if it is benchmark strategy then set it
    if (strategy.get()->get_strategy_type() == AgisStrategyType::BENCHMARK)
    {
        this->benchmark_strategy = static_cast<BenchMarkStrategy*>(strategy.get().get());
    }
    else {
        this->strategies.emplace(
            strategy.get()->get_strategy_index(),
            strategy.get().get()
        );
        this->strategy_ids.emplace(
            strategy.get()->get_strategy_id(),
            strategy.get()->get_strategy_index()
        );
    }
    UNLOCK_GUARD
}


//============================================================================
void Portfolio::__reset()
{
    LOCK_GUARD
    this->positions.clear();
    this->unrealized_pl = 0;

    this->position_history.clear();
    this->trade_history.clear();
    this->tracers.reset_history();
    UNLOCK_GUARD
}


//============================================================================
void Portfolio::__remove_strategy(size_t index)
{
    LOCK_GUARD
    auto it = std::find_if(strategy_ids.begin(), strategy_ids.end(),
        [index](const auto& pair) {
            return pair.second == index;
        });
    if (it != strategy_ids.end()) {
        // If the value is found, erase the corresponding pair using the iterator
        strategy_ids.erase(it);
    }
    this->strategies.erase(index);
    UNLOCK_GUARD
}


//============================================================================
void Portfolio::__remember_order(SharedOrderPtr order)
{
    LOCK_GUARD
    AgisStrategy* strategy = nullptr;
    if (order->phantom_order) strategy = this->benchmark_strategy;
    else strategy = this->strategies.at(order.get()->get_strategy_index());
    strategy->__remember_order(order);
    UNLOCK_GUARD
}

void Portfolio::__on_assets_expired(AgisRouter& router, ThreadSafeVector<size_t> const& ids)
{
    LOCK_GUARD
    for (auto& id : ids)
    {
        auto position = this->get_position(id);
        if (!position.has_value()) { continue; }

        auto order = position.value().get()->generate_position_inverse();
        order->__set_state(OrderState::CHEAT);
        order->__set_force_close(true);
        router.place_order(std::move(order));
    }
    UNLOCK_GUARD
}


//============================================================================
void Portfolio::open_position(OrderPtr const& order)
{
    auto& strategy = this->strategies.at(order->get_strategy_index());
    this->positions.emplace(
        order->get_asset_index(), 
        std::make_unique<Position>(strategy, order)
    );
}


//============================================================================
void Portfolio::__on_trade_closed(size_t start_index)
{
    if (start_index == this->trade_history.size()) { return; }
    for (auto i = start_index; i < this->trade_history.size(); i++)
    {
        SharedTradePtr trade = this->trade_history[i];

        // check for trade partitions to close
        if (trade->child_partitions.size()) {
            for (auto& partition : trade->child_partitions) {
                auto order = partition->child_trade->generate_trade_inverse();
                order->set_units(-1*partition->child_trade_units);
                order->__set_state(OrderState::CHEAT);
                this->router.place_order(std::move(order));
            }
        }

        auto& strategy = this->strategies.at(trade->strategy_index);
        strategy->__remember_trade(trade);
    }
}


//============================================================================
void Portfolio::modify_position(OrderPtr const& order)
{
    auto& position = this->__get_position(order->get_asset_index());
    auto& strategy = this->strategies.at(order->get_strategy_index());
    auto idx = this->trade_history.size();
    position->adjust(strategy, order, this->trade_history);
    this->__on_trade_closed(idx);
}


//============================================================================
void Portfolio::close_position(OrderPtr const& order)
{
    // close the position obj
    auto asset_id = order->get_asset_index();
    auto& position = this->__get_position(asset_id);
    auto idx = this->trade_history.size();
    position->close(order, this->trade_history);
    this->__on_trade_closed(idx);

    // remove from portfolio and remember
    PositionPtr closed_position = std::move(this->positions.at(asset_id));
    this->unrealized_pl -= closed_position->unrealized_pl;
    this->position_history.push_back(std::move(closed_position));
    this->positions.erase(asset_id);
}


//============================================================================
double Frictions::calculate_frictions(OrderPtr const& order)
{
    double friction = 0;
    double v;
    if (this->flat_commisions.has_value())
    {
		v = this->flat_commisions.value();
        this->total_flat_commisions += v;
		friction += v;
	}
    if (this->per_unit_commisions.has_value())
    {
        v = this->per_unit_commisions.value() * order->get_units();
        this->total_per_unit_commisions += v;
        friction += v;
    }
    if (this->slippage.has_value())
    {
		auto v = this->slippage.value() * abs(order->get_units());
        this->total_slippage += v;
        friction += v;
	}
    return friction;
}