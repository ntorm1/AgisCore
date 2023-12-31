#include "pch.h"
#include "Trade.h"
#include "Order.h"
#include "AgisStrategy.h"
#include "Hydra.h"
#include "Portfolio.h"

#include "Asset/Asset.h"
#include "Broker/Broker.Base.h"

using namespace Agis;
using namespace rapidjson;

std::atomic<size_t> Trade::trade_counter(0);


//============================================================================
Trade::Trade(AgisStrategy* strategy_, OrderPtr const& filled_order):
    strategy(strategy_),
    broker(filled_order->__broker),
    __asset(filled_order->__asset)
{
    this->asset_index = filled_order->get_asset_index();
    this->strategy_index = filled_order->get_strategy_index();
    this->portfolio_index = filled_order->get_portfolio_index();

    // set the trade member variables
    this->units = filled_order->get_units();
    this->units_multiplier = __asset->get_unit_multiplier();
    this->average_price = filled_order->get_average_price();
    this->open_price = this->average_price;


    switch (this->__asset->get_asset_type()) {
    case AssetType::US_EQUITY: {
        this->nlv = this->units * this->average_price * units_multiplier;
        this->margin = filled_order->get_margin_impact();
        this->collateral = filled_order->get_cash_impact();
        this->nlv -= this->margin;
        break;
    }
    case AssetType::US_FUTURE: {
        this->nlv = filled_order->get_cash_impact();
        this->margin = filled_order->get_margin_impact();
        this->collateral = filled_order->get_cash_impact();
        break;
    }
    default:
        break;
    }

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
    this->realized_pl +=(this->units * this->units_multiplier * (this->close_price - this->average_price));
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
    auto adjustment = -1 * (units_ * this->units_multiplier * (filled_order->get_average_price()-this->average_price));
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
    if (units_ * this->units > 0){
        this->increase(filled_order);
    }
    else{
        this->reduce(filled_order);
    }

    // update margin balances held in the trade
    this->margin += filled_order->get_margin_impact();
    this->collateral += filled_order->get_cash_impact();

    // set the filled orders parent
    filled_order->parent_trade = this;
}


//============================================================================
void Trade::evaluate_stock(double market_price) noexcept
{
    auto nlv_new = this->units * market_price * this->units_multiplier;
    nlv_new -= this->margin;
    auto unrealized_pl_new = this->units * this->units_multiplier * (market_price - this->average_price);

    // adjust strategy levels 
    strategy->tracers.nlv_add_assign(nlv_new);
    strategy->unrealized_pl += (unrealized_pl_new - this->unrealized_pl);

    // if strategy is tracing volatility, set the portfolio weight equah to the nlv of the trade
    // must be divided by the total nlv of the strategy after all trades are evaluated
    if (strategy->tracers.has(Tracer::VOLATILITY)) {
        strategy->tracers.set_portfolio_weight(this->asset_index, nlv_new);
    }

    this->nlv = nlv_new;
    this->unrealized_pl = unrealized_pl_new;
}


//============================================================================
void Trade::evaluate_future(double market_price, bool is_reprice) noexcept
{
    // futures are evaluated by either posting or releasing collateral to maintain the trade
    bool is_eod = this->__asset->__is_eod;
    double margin_req;
    if (is_eod) {
        if (this->units < 0) {
            margin_req = broker->get_margin_requirement(this->asset_index, MarginType::SHORT_OVERNIGHT_MAINTENANCE).value();
        }
        else {
            margin_req = broker->get_margin_requirement(this->asset_index, MarginType::OVERNIGHT_MAINTENANCE).value();
        }
    }
    else {
        margin_req = broker->get_margin_requirement(this->asset_index, MarginType::INTRADAY_MAINTENANCE).value();
    }
    
    // adjust cash levels base on new collateral requirements
    if (is_reprice) {
        this->margin = (1 - margin_req) * abs(this->units) * this->units_multiplier * market_price;
        auto new_collateral = margin_req * abs(this->units) * this->units_multiplier * market_price;
        auto collateral_adjustment = this->collateral - new_collateral;
        auto cash_adjustment = (market_price - this->last_price) * this->units * this->units_multiplier;
        cash_adjustment += collateral_adjustment;

        this->collateral = new_collateral;
        this->strategy->tracers.cash_add_assign(cash_adjustment);
        this->strategy->portfolio->tracers.cash_add_assign(cash_adjustment);
    }
    this->nlv = this->collateral;
    this->strategy->tracers.nlv_add_assign(nlv);

    // if strategy is tracing volatility, set the portfolio weight equah to the nlv of the trade
    // must be divided by the total nlv of the strategy after all trades are evaluated
    if (strategy->tracers.has(Tracer::VOLATILITY)) {
        strategy->tracers.set_portfolio_weight(
            this->asset_index,
            this->units * market_price * this->units_multiplier
        );
    }
}


//============================================================================
void Trade::evaluate(double market_price, bool on_close, bool is_reprice)
{
    // adjust the source strategy nlv and unrealized pl
    switch (this->__asset->get_asset_type()) {
        case AssetType::US_EQUITY:
			this->evaluate_stock(market_price);
			break;
		case AssetType::US_FUTURE:
			this->evaluate_future(market_price, is_reprice);
			break;
		default:
			break;
    }

    // adjust strategy net beta levels
    if (strategy->tracers.has(Tracer::BETA))
    {
        auto beta_dollars = (
            this->units * market_price * __asset->get_beta().value_or(0.0f)
            );
        strategy->tracers.net_beta_add_assign(beta_dollars);
    }

    // adjust the strategy net leverage ratio to the abs of the position value
    if (strategy->tracers.has(Tracer::LEVERAGE)) {
        strategy->tracers.net_leverage_ratio_add_assign(
            abs(this->units) * market_price * this->units_multiplier
        );
    }

    this->last_price = market_price;
    if (on_close && !is_reprice) { this->bars_held++; }
}


//============================================================================
std::expected<rapidjson::Document, AgisException>
Trade::serialize(HydraPtr hydra) const
{
    Document trade(kObjectType);

    trade.AddMember("Trade Open Time", trade_open_time, trade.GetAllocator());
    trade.AddMember("Trade Close Time", trade_close_time, trade.GetAllocator());

    trade.AddMember("Bars Held", bars_held, trade.GetAllocator());
    trade.AddMember("Units", units, trade.GetAllocator());
    trade.AddMember("Average Price", average_price, trade.GetAllocator());
    trade.AddMember("Close Price", close_price, trade.GetAllocator());
    trade.AddMember("Unrealized PL", unrealized_pl, trade.GetAllocator());
    trade.AddMember("Realized PL", realized_pl, trade.GetAllocator());
    trade.AddMember("Trade Identifier", trade_id, trade.GetAllocator());
    trade.AddMember("NLV", nlv, trade.GetAllocator());
    trade.AddMember("Last Price", last_price, trade.GetAllocator());

    Value asset_id, strategy_id, portfolio_id;
    try {
        asset_id.SetString(hydra->asset_index_to_id(asset_index).unwrap().c_str(), trade.GetAllocator());
        strategy_id.SetString(hydra->strategy_index_to_id(strategy_index).unwrap().c_str(), trade.GetAllocator());
        portfolio_id.SetString(hydra->portfolio_index_to_id(portfolio_index).unwrap().c_str(), trade.GetAllocator());
    }
    catch (AgisException& e) {
        return std::unexpected<AgisException> {e.what()};
	}

    trade.AddMember("Asset Identifier", asset_id, trade.GetAllocator());
    trade.AddMember("Strategy Identifier", strategy_id, trade.GetAllocator());
    trade.AddMember("Portfolio Identifier", portfolio_id, trade.GetAllocator());

    return trade;
}


//============================================================================
std::shared_ptr<TradePartition>
Trade::get_child_partition(size_t asset_index)
{
    for (auto& partition : this->child_partitions)
    {
        if (partition->child_trade->asset_index == asset_index) return partition;
    }
    return nullptr;
}


//============================================================================
bool
Trade::partition_exists(size_t asset_index)
{
    for (auto& partition : this->child_partitions)
    {
        if (partition->child_trade->asset_index == asset_index) return true;
    }
    return false;
}


//============================================================================
bool
Trade::order_closes(std::reference_wrapper<OrderPtr> new_order_ref) const noexcept
{
    return (abs(this->units + new_order_ref.get()->get_units()) < DBL_EPSILON);
}


//============================================================================
bool
Trade::order_reduces(std::reference_wrapper<OrderPtr> new_order_ref) const noexcept
{
    if (std::signbit(this->units) == std::signbit(new_order_ref.get()->get_units())) {
        return false;
    }
    return true;
}


//============================================================================
bool
Trade::order_flips(std::reference_wrapper<OrderPtr> new_order_ref) const noexcept
{
    if (std::signbit(this->units) == std::signbit(new_order_ref.get()->get_units())) {
		return false;
	}
    if (abs(this->units) < abs(new_order_ref.get()->get_units())) {
		return true;
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
        this->portfolio_index,
        this->strategy->get_broker_index()
    );
}