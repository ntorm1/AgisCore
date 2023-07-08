#include "pch.h"

#include "Portfolio.h"

std::atomic<size_t> Trade::trade_counter(0);
std::atomic<size_t> Position::position_counter(0);
std::atomic<size_t> Portfolio::portfolio_counter(0);


Trade::Trade(OrderPtr const& filled_order)
{
    this->asset_id = filled_order->get_asset_index();
    this->strategy_id = filled_order->get_strategy_index();

    // set the trade member variables
    this->units = filled_order->get_units();
    this->average_price = filled_order->get_average_price();
    this->unrealized_pl = 0;
    this->realized_pl = 0;
    this->close_price = 0;
    this->last_price = filled_order->get_average_price();
    this->nlv = this->units * this->average_price;

    // set the times
    this->trade_close_time = 0;
    this->trade_open_time = filled_order->get_fill_time();
    this->trade_id = trade_counter++;
}


void Trade::close(OrderPtr const& filled_order)
{
    this->close_price = filled_order->get_average_price();
    this->trade_close_time = filled_order->get_fill_time();
    this->realized_pl += this->units * (this->close_price - this->average_price);
    this->unrealized_pl = 0;
}

void Trade::increase(OrderPtr const& filled_order)
{
    auto units_ = filled_order->get_units();
    auto p = filled_order->get_average_price();
    double new_units = abs(this->units) + abs(units_);
    this->average_price = ((abs(this->units) * this->average_price) + (abs(units_) * p)) / new_units;
    this->units += units_;
}
void Trade::reduce(OrderPtr const& filled_order)
{
    auto units_ = filled_order->get_units();
    this->realized_pl += abs(units_) * (filled_order->get_average_price() - this->average_price);
    this->units += units_;
}

void Trade::adjust(OrderPtr const& filled_order)
{
    // extract order information
    auto units_ = filled_order->get_units();
    if (units_ * this->units > 0)
    {
        this->increase(filled_order);
    }
    else
    {
        this->reduce(filled_order);
    }
}

Position::Position(OrderPtr const& filled_order_)
{
    //populate common position values
    this->asset_id = filled_order_->get_asset_index();
    this->units = filled_order_->get_units();

    // populate order values
    this->nlv = filled_order_->get_units() * filled_order_->get_average_price();
    this->average_price = filled_order_->get_average_price();
    this->last_price = filled_order_->get_average_price();
    this->position_open_time = filled_order_->get_fill_time();

    // insert the new trade
    auto trade = std::make_unique<Trade>(
        filled_order_
    );
    this->trades.insert({trade->strategy_id,std::move(trade)});
    this->position_id = position_counter++;

}

Trade* Position::__get_trade(size_t strategy_index)
{
    auto it = this->trades.find(strategy_index);
    if (it != trades.end())
    {
        return it->second.get();
    }
    return nullptr;
}

void Position::close(OrderPtr const& order, std::vector<TradePtr>& trade_history)
{
    // close the position
    auto price = order->get_average_price();
    this->close_price = price;
    this->position_close_time = order->get_fill_time();
    this->realized_pl += this->units * (price - this->average_price);
    this->unrealized_pl = 0;

    for (auto& element : trades) {
        // Retrieve the unique pointer
        auto trade = std::move(element.second);

        // Add the unique pointer to the vector
        trade_history.push_back(std::move(trade));
    }
}

void Position::adjust(OrderPtr const& order, std::vector<TradePtr>& trade_history)
{
    auto units_ = order->get_units();
    auto fill_price = order->get_average_price();

    // increasing position
    if (units_ * this->units > 0)
    {
        double new_units = abs(this->units) + abs(units_);
        this->average_price = ((abs(this->units) * this->average_price) + (abs(units_) * fill_price)) / new_units;
    }
    // reducing position
    else
    {
        this->realized_pl += abs(units_) * (fill_price - this->average_price);
    }

    // adjust position units
    this->units += units_;

    //test to see if strategy already has a trade
    auto strategy_id = order->get_strategy_index();
    auto trad_opt = this->__get_trade(strategy_id);
    if (!trad_opt)
    {
        auto trade = std::make_unique<Trade>(order);
        this->trades.insert({ strategy_id,std::move(trade) });
    }
    else
    {
        if (abs(trad_opt->units + units_) < 1e-10)
        {
            trad_opt->close(order);
            auto extracted_trade = std::move(this->trades.at(strategy_id));
            trade_history.push_back(std::move(extracted_trade));
        }
        else
        {
            trad_opt->adjust(order);
        }
    }
}

void PortfolioMap::__on_order_fill(OrderPtr const& order)
{
    auto& portfolio = this->portfolios[order->get_portfolio_index()];
    portfolio->__on_order_fill(order);
}

void PortfolioMap::__register_portfolio(PortfolioPtr portfolio)
{
    this->portfolio_map.emplace(portfolio->__get_portfolio_id(), portfolio->__get_index());
    this->portfolios.emplace(portfolio->__get_index(), std::move(portfolio));
}

PortfolioPtr const& PortfolioMap::__get_portfolio(std::string const& id)
{
    auto portfolio_index = this->portfolio_map.at(id);
    return this->portfolios.at(portfolio_index);
}

Portfolio::Portfolio(std::string portfolio_id_, double cash_)
{
    this->portfolio_id = portfolio_id_;
    this->cash = cash_;
    this->portfolio_index = portfolio_counter++;
}

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
        else
        {
            this->close_position(order);
        }
    }
    // adjust account levels
    auto amount = gmp_mult(order->get_units(), order->get_average_price());
    gmp_sub_assign(this->cash, amount);
    UNLOCK_GUARD
}

void Portfolio::open_position(OrderPtr const& order)
{
    this->positions.emplace(
        order->get_asset_index(), 
        std::make_unique<Position>(order)
    );
}

void Portfolio::modify_position(OrderPtr const& order)
{
    auto& position = this->__get_position(order->get_asset_index());
    position->adjust(order, trade_history);
}

void Portfolio::close_position(OrderPtr const& order)
{
    // close the position obj
    auto asset_id = order->get_asset_index();
    auto& position = this->__get_position(asset_id);
    position->close(order, this->trade_history);

    // remove from portfolio and remember
    auto closed_position = std::move(this->positions.at(asset_id));
    this->position_history.push_back(std::move(position));
}
