#include "pch.h"
#include <tbb/parallel_for_each.h>
#include <algorithm>

#include "Portfolio.h"
#include "AgisStrategy.h"

std::atomic<size_t> Position::position_counter(0);
std::atomic<size_t> Portfolio::portfolio_counter(0);


//============================================================================
Position::Position(AgisStrategyRef strategy, OrderPtr const& filled_order_)
{
    //populate common position values
    this->asset_id = filled_order_->get_asset_index();
    this->portfolio_id = filled_order_->get_portfolio_index();
    this->units = filled_order_->get_units();

    // populate order values
    this->nlv = filled_order_->get_units() * filled_order_->get_average_price();
    this->average_price = filled_order_->get_average_price();
    this->last_price = filled_order_->get_average_price();
    this->position_open_time = filled_order_->get_fill_time();

    // insert the new trade
    auto trade = std::make_unique<Trade>(
        strategy,
        filled_order_
    );
    this->trades.insert({trade->strategy_id,std::move(trade)});
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
void Position::__evaluate(ThreadSafeVector<OrderPtr>& orders, double market_price, bool on_close)
{
    this->last_price = market_price;
    this->unrealized_pl = this->units*(market_price-this->average_price);
    this->nlv = market_price * this->units;
    if (on_close) { this->bars_held++; }

    for (auto& trade_pair : this->trades) 
    {
        auto& trade = trade_pair.second;
        trade->evaluate(market_price, on_close);

        // test trade exit
        if (!trade->exit.has_value()) { continue; }
        if (trade->exit.value()->exit())
        {
            auto order = trade->generate_trade_inverse();
            // set the state to Cheat to allow for the order to be filled and then processed by
            // the portfolio in a single call to order router
            order->__set_state(OrderState::CHEAT);
            orders.push_back(std::move(order));
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
        // Retrieve the unique pointer
        auto trade = std::move(element.second);
        trade->close(order);

        // Add the unique pointer to the vector
        SharedTradePtr ptr = std::move(trade);
        trade_history.push_back(ptr);
    }
}


//============================================================================
void Position::adjust(AgisStrategyRef strategy, OrderPtr const& order, std::vector<SharedTradePtr>& trade_history)
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
    if (!trade_opt)
    {
        auto trade = std::make_unique<Trade>(
            strategy,
            order
        );
        this->trades.insert({ strategy_id,std::move(trade) });
    }
    else
    {
        auto& trade_ptr = trade_opt->get();
        if (abs(trade_ptr->units + units_) < 1e-10)
        {
            trade_ptr->close(order);
            TradePtr extracted_trade = std::move(this->trades.at(strategy_id));
            this->trades.erase(strategy_id);
            SharedTradePtr ptr = std::move(extracted_trade);
            trade_history.push_back(ptr);
        }
        else
        {
            trade_ptr->adjust(order);
        }
    }
}


//============================================================================
OrderPtr Position::generate_position_inverse()
{   
    return std::make_unique<MarketOrder>(
        this->asset_id,
        -1 * this->units,
        DEFAULT_STRAT_ID,
        this->portfolio_id
    );
}


//============================================================================
void PortfolioMap::__evaluate(AgisRouter& router, ExchangeMap const& exchanges, bool on_close)
{
    // Define a lambda function that calls next for each strategy
    auto portfolio_evaluate = [&](auto& portfolio) {
        portfolio.second->__evaluate(router, exchanges, on_close);
    };

    tbb::parallel_for_each(
        this->portfolios.begin(),
        this->portfolios.end(),
        portfolio_evaluate
    );
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
void PortfolioMap::__on_order_fill(OrderPtr const& order)
{
    auto& portfolio = this->portfolios[order->get_portfolio_index()];
    portfolio->__on_order_fill(order);
}

void PortfolioMap::__remember_order(OrderRef order)
{
    auto& portfolio = this->portfolios.at(order.get()->get_portfolio_index());
    portfolio->__remember_order(std::move(order));
}

void PortfolioMap::__on_assets_expired(AgisRouter& router, ThreadSafeVector<size_t> const& ids)
{
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
void PortfolioMap::__register_strategy(AgisStrategyRef strategy)
{
    auto& portfolio = this->portfolios.at(strategy.get()->get_portfolio_index());
    portfolio->register_strategy(std::move(strategy));
}


//============================================================================
PortfolioPtr const& PortfolioMap::__get_portfolio(std::string const& id)
{
    auto portfolio_index = this->portfolio_map.at(id);
    return this->portfolios.at(portfolio_index);
}


//============================================================================
json Portfolio::to_json() const
{
    json strategies;
    for (const auto& strategy : this->strategies)
    {
        json strat_json;
        strategy.second.get()->to_json(strat_json);
        strategies.push_back(strat_json);
    }

    auto j = json{
        {"starting_cash", this->starting_cash},
    };
    j["strategies"] = strategies;
    return j;
}


//============================================================================
void PortfolioMap::restore(json const& j)
{
    json portfolios = j["portfolios"];

    // Store the exchange items in a vector for processing
    for (const auto& portfolio : portfolios.items())
    {
        std::string portfolio_id = portfolio.key();
        json& j = portfolio.value();
        double starting_cash = j["starting_cash"];
        
        // build new portfolio from the parsed settings
        auto portfolio = std::make_unique<Portfolio>(portfolio_id, starting_cash);
        this->__register_portfolio(std::move(portfolio));
    }
}


//============================================================================
AGIS_API json PortfolioMap::to_json() const
{
    json j;
    for (const auto& pair : this->portfolios) {
        j[pair.second->__get_portfolio_id()] = pair.second->to_json();
    }
    return j;
}


//============================================================================
Portfolio::Portfolio(std::string portfolio_id_, double cash_)
{
    this->portfolio_id = portfolio_id_;
    this->cash = cash_;
    this->starting_cash = cash_;
    this->nlv = cash_;
    this->portfolio_index = portfolio_counter++;
}


//============================================================================
void Portfolio::__on_order_fill(OrderPtr const& order)
{
    LOCK_GUARD
    auto asset_index = order->get_asset_index();
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
        else if (
            !position->__trade_exits(order->get_strategy_index()) ||
            position->__get_trade_count() > 1
            )
        {
            this->modify_position(order);
        }
        else
        {
            this->close_position(order);
        }
    }
    // adjust account levels
    auto amount = order->get_units() * order->get_average_price();
    this->cash -= amount;
    UNLOCK_GUARD
}


//============================================================================
void Portfolio::__evaluate(AgisRouter& router, ExchangeMap const& exchanges, bool on_close)
{
    this->nlv = this->cash;
    this->unrealized_pl = 0;
    ThreadSafeVector<OrderPtr> orders;
    for (auto it = this->positions.begin(); it != positions.end(); ++it)
    {
        // attempt to get the current market price of the underlying asset of the position
        auto& position = it->second;
        auto market_price = exchanges.__get_market_price(position->asset_id, on_close);
        if(market_price == 0.0)
        {
            continue;
        }
        
        // evaluate the indivual positions and allow for any orders that are generated
        // as a result of the new valuation
        position->__evaluate(orders, market_price, on_close);
        this->nlv += position->nlv;
        this->unrealized_pl += position->unrealized_pl;
    }
    if (orders.size())
    {
        for (int i = 0; i < orders.size(); i++) {
            std::optional<OrderPtr> order = orders.pop_back();
            router.place_order(std::move(order.value()));
        }
    }
    this->nlv_history.push_back(this->nlv);
    this->cash_history.push_back(this->cash);

    // log strategy levels
    for (const auto& strat : this->strategies)
    {
        strat.second.get()->__evaluate(on_close);
    }

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
AGIS_API std::optional<TradeRef> Portfolio::get_trade(size_t asset_index, std::string const& strategy_id)
{
    auto index = this->strategy_ids.at(strategy_id);
    auto position = this->get_position(asset_index);
    if (!position.has_value()) { return std::nullopt; }
    return position.value().get()->__get_trade(index);
}


//============================================================================
AGIS_API std::vector<size_t> Portfolio::get_strategy_positions(size_t strategy_index) const
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
    return v;
}


//============================================================================
AGIS_API void Portfolio::register_strategy(AgisStrategyRef strategy)
{
    this->strategies.emplace(
        strategy.get()->get_strategy_index(),
        strategy
    );
    this->strategy_ids.emplace(
        strategy.get()->get_strategy_id(),
        strategy.get()->get_strategy_index()
    );
}


//============================================================================
void Portfolio::__reset()
{
    this->nlv = this->starting_cash;
    this->cash = this->starting_cash;
    this->positions.clear();
    this->unrealized_pl = 0;

    this->position_history.clear();
    this->trade_history.clear();
    this->nlv_history.clear();
    this->cash_history.clear();
}


//============================================================================
void Portfolio::__remember_order(OrderRef order)
{
    LOCK_GUARD
    auto& strategy = this->strategies.at(order.get()->get_strategy_index());
    strategy.get()->__remember_order(order);
    UNLOCK_GUARD
}

void Portfolio::__on_assets_expired(AgisRouter& router, ThreadSafeVector<size_t> const& ids)
{
    for (auto& id : ids)
    {
        auto position = this->get_position(id);
        if (!position.has_value()) { continue; }

        auto order = position.value().get()->generate_position_inverse();
        order->__set_state(OrderState::CHEAT);
        router.place_order(std::move(order));
    }
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
        auto& strategy = this->strategies.at(trade->strategy_id);
        strategy.get()->__remember_trade(trade);
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
