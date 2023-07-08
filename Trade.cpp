#include "pch.h"
#include "Trade.h"

std::atomic<size_t> Trade::trade_counter(0);


Trade::Trade(OrderPtr const& filled_order)
{
    this->asset_id = filled_order->get_asset_index();
    this->strategy_id = filled_order->get_strategy_index();
    this->portfolio_id = filled_order->get_portfolio_index();

    // set the trade member variables
    this->units = filled_order->get_units();
    this->average_price = filled_order->get_average_price();
    this->unrealized_pl = 0;
    this->realized_pl = 0;
    this->close_price = 0;
    this->last_price = filled_order->get_average_price();

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

OrderPtr Trade::generate_trade_inverse() {
    return std::make_unique<MarketOrder>(
        this->asset_id,
        -1 * this->units,
        this->strategy_id,
        this->portfolio_id
    );
}