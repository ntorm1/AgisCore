#include "pch.h"
#include "Trade.h"
#include "Order.h"
#include "AgisStrategy.h"
#include "Hydra.h"


std::atomic<size_t> Trade::trade_counter(0);


//============================================================================
Trade::Trade(AgisStrategy* strategy_, OrderPtr const& filled_order):
    strategy(strategy_),
    __asset(filled_order->__asset)
{
    this->asset_index = filled_order->get_asset_index();
    this->strategy_index = filled_order->get_strategy_index();
    this->portfolio_index = filled_order->get_portfolio_index();

    // set the trade member variables
    this->units = filled_order->get_units();
    this->average_price = filled_order->get_average_price();
    this->open_price = this->average_price;
    this->nlv = this->units * this->average_price;
    this->unrealized_pl = 0;
    this->realized_pl = 0;
    this->close_price = 0;
    this->last_price = this->average_price;

    if (filled_order->has_exit()) {
        this->exit = filled_order->move_exit();
        this->exit.value()->build(this); 
    }

    // set the filled orders parent
    filled_order->parent_trade = this;

    // set the times
    this->trade_close_time = 0;
    this->trade_open_time = filled_order->get_fill_time();
    this->bars_held = 0;
    this->trade_id = trade_counter++;
}


//============================================================================
void Trade::close(OrderPtr const& filled_order)
{
    this->close_price = filled_order->get_average_price();
    this->trade_close_time = filled_order->get_fill_time();
    this->realized_pl +=(this->units * (this->close_price - this->average_price));
    this->unrealized_pl = 0;

    // tell the strategy we are closing
    this->strategy->__on_trade_closed(this->asset_index);

}


//============================================================================
void Trade::increase(OrderPtr const& filled_order)
{
    auto units_ = filled_order->get_units();
    auto p = filled_order->get_average_price();
    double new_units = (abs(this->units) + abs(units_));
    this->average_price = ((abs(this->units) * this->average_price) + (abs(units_) * p)) / new_units;
    this->units += units_;
}


//============================================================================
void Trade::reduce(OrderPtr const& filled_order)
{
    auto units_ = filled_order->get_units();
    auto adjustment = -1 * (units_*(filled_order->get_average_price()-this->average_price));
    strategy->unrealized_pl -= adjustment;
    this->realized_pl += adjustment;
    this->unrealized_pl -= adjustment;
    this->units += units_;
}


//============================================================================
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

    // set the filled orders parent
    filled_order->parent_trade = this;
}


//============================================================================
void Trade::evaluate(double market_price, bool on_close, bool is_reprice)
{
    // adjust the source strategy nlv and unrealized pl
    auto nlv_new = this->units * market_price;
    auto unrealized_pl_new = this->units*(market_price-this->average_price);
    
    // adjust strategy levels 
    strategy->nlv += nlv_new;
    strategy->unrealized_pl += (unrealized_pl_new - this->unrealized_pl);

    // adjust strategy net beta levels
    if (strategy->net_beta.has_value())
    {
        auto beta_dollars = (
            this->units * market_price * __asset->get_beta().unwrap_or(0.0f)
        );
        strategy->net_beta.value() += beta_dollars;
    }

    // adjust the strategy net leverage ratio to the abs of the position value
    if (strategy->net_leverage_ratio.has_value()) {
        strategy->net_leverage_ratio.value() += (
			abs(this->units) * market_price
		);
    }

    this->nlv = nlv_new;
    this->unrealized_pl = unrealized_pl_new;
    this->last_price = market_price;
    if (on_close && !is_reprice) { this->bars_held++; }
}


//============================================================================
AgisResult<json> Trade::serialize(json& _json, HydraPtr hydra) const
{
    if (_json.size()) { _json.clear(); }

    _json["Trade Open Time"] = this->trade_open_time;
    _json["Trade Close Time"] = this->trade_close_time;
    _json["Bars Held"] = this->bars_held;
    _json["Units"] = this->units;
    _json["Average Price"] = this->average_price;
    _json["Close Price"] = this->close_price;
    _json["Unrealized PL"] = this->unrealized_pl;
    _json["Realized PL"] = this->realized_pl;
    _json["Trade Identifier"] = this->trade_id;
    _json["NLV"] = this->nlv;
    _json["Last Price"] = this->last_price;
    AGIS_ASSIGN_OR_RETURN(_json["Asset Identifier"], hydra->asset_index_to_id(this->asset_index), std::string, json);
    AGIS_ASSIGN_OR_RETURN(_json["Strategy Identifier"], hydra->strategy_index_to_id(this->strategy_index), std::string, json);
    AGIS_ASSIGN_OR_RETURN(_json["Portfolio Identifier"], hydra->portfolio_index_to_id(this->portfolio_index), std::string, json);

    return AgisResult<json>(_json);
}


//============================================================================
std::shared_ptr<TradePartition> Trade::get_child_partition(size_t asset_index)
{
    for (auto& partition : this->child_partitions)
    {
        if (partition->child_trade->asset_index == asset_index) return partition;
    }
    return nullptr;
}


//============================================================================
bool Trade::partition_exists(size_t asset_index)
{
    for (auto& partition : this->child_partitions)
    {
        if (partition->child_trade->asset_index == asset_index) return true;
    }
    return false;
}


//============================================================================
OrderPtr Trade::generate_trade_inverse() {
    return std::make_unique<Order>(
        OrderType::MARKET_ORDER,
        this->asset_index,
        -1 * this->units,
        this->strategy_index,
        this->portfolio_index
    );
}