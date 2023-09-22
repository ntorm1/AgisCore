#include "pch.h"
#ifdef USE_LUAJIT
#include <fstream>
#include "AbstractStrategyTree.h"
#include "AgisLuaStrategy.h"
#include "AgisObservers.h"
#include "AgisFunctional.h"

SOL_BASE_CLASSES(AgisLuaStrategy, AgisStrategy);
SOL_DERIVED_CLASSES(AgisLuaStrategy, AgisLuaStrategy);



//============================================================================
void init_lua_enums(sol::state& lua)
{
	lua.new_enum<ExchangeQueryType>("ExchangeQueryType",
		{
			{"Default", ExchangeQueryType::Default},
			{"NLargest", ExchangeQueryType::NLargest},
			{"NSmallest", ExchangeQueryType::NSmallest},
			{"NExtreme", ExchangeQueryType::NExtreme}
		}
	);
	lua.new_enum<AgisOpperationType>("AgisOpperationType",
		{
			{"INIT", AgisOpperationType::INIT},
			{"IDENTITY", AgisOpperationType::IDENTITY},
			{"ADD", AgisOpperationType::ADD},
			{"SUBTRACT", AgisOpperationType::SUBTRACT},
			{"MULTIPLY", AgisOpperationType::MULTIPLY},
			{"DIVIDE", AgisOpperationType::DIVIDE}
		}
	);
	lua.new_enum<ExchangeViewOpp>("ExchangeViewOpp",
		{
			{"UNIFORM", ExchangeViewOpp::UNIFORM},
			{"LINEAR_DECREASE", ExchangeViewOpp::LINEAR_DECREASE},
			{"LINEAR_INCREASE", ExchangeViewOpp::LINEAR_INCREASE},
			{"CONDITIONAL_SPLIT", ExchangeViewOpp::CONDITIONAL_SPLIT},
			{"UNIFORM_SPLIT", ExchangeViewOpp::UNIFORM_SPLIT},
			{"CONSTANT", ExchangeViewOpp::CONSTANT}
		}
	);
	lua.new_enum<AllocType>("AllocType",
		{
			{"UNITS", AllocType::UNITS},
			{"DOLLARS", AllocType::DOLLARS},
			{"PCT", AllocType::PCT},
		}
	);
	lua.new_enum<AssetObserverType>("AssetObserverType",
		{
			{"COL_ROL_MEAN", AssetObserverType::COL_ROL_MEAN}
		}
	);
}

//============================================================================
void init_lua_interface(sol::state* lua_ptr) {
	auto& lua = *lua_ptr;
	lua.open_libraries(sol::lib::base);
	init_lua_enums(lua);

	// register ast nodes
	lua.new_usertype<AbstractAssetLambdaRead>("AbstractAssetLambdaRead",
		sol::no_constructor
	);
	lua.new_usertype<AbstractAssetLambdaOpp>("AbstractAssetLambdaOpp",
		sol::no_constructor
	);
	//lua.new_usertype<AbstractAssetLambdaFilter>("AbstractAssetLambdaFilter",
	//	sol::no_constructor
	//);
	lua.new_usertype<AbstractExchangeNode>("AbstractExchangeNode",
		sol::no_constructor
	);
	lua.new_usertype<AbstractExchangeViewNode>("AbstractExchangeViewNode",
		sol::no_constructor
	);
	lua.new_usertype<AbstractSortNode>("AbstractSortNode",
				sol::no_constructor
	);
	lua.new_usertype<AbstractGenAllocationNode>("AbstractGenAllocationNode",
		sol::no_constructor
	);
	lua.new_usertype<AbstractStrategyAllocationNode>("AbstractStrategyAllocationNode",
		sol::no_constructor
	);

	//lua.set_function("create_asset_lambda_filter", create_asset_lambda_filter);
	lua.set_function("create_asset_lambda_read", create_asset_lambda_read);
	lua.set_function("create_asset_lambda_opp", create_asset_lambda_opp);
	lua.set_function("create_exchange_node", create_exchange_node);
	lua.set_function("create_exchange_view_node", create_exchange_view_node);
	lua.set_function("create_gen_alloc_node", create_gen_alloc_node);
	lua.set_function("create_sort_node", create_sort_node);
	lua.set_function("create_strategy_alloc_node", create_strategy_alloc_node);

	// Register the AgisResult template and its methods and constructors
	lua.new_usertype<AgisResult<double>>(
		"AgisResult",
		sol::no_constructor,
		"is_exception", &AgisResult<double>::is_exception,
		"unwrap", &AgisResult<double>::unwrap
	);


	// Bind the Asset class with no constructors.
	lua.new_usertype<Asset>("Asset",
		sol::no_constructor,
		"get_asset_feature",
		sol::overload(
			[](const Asset& asset, const std::string& col, int index) {
				return asset.get_asset_feature(col, index);
			},
			[](const Asset& asset, size_t col, int index) {
				return asset.get_asset_feature(col, index);
			}
		)
	);

	auto exchange_add_observer_lambda = [](
		ExchangePtr const exchange,
		sol::variadic_args va)
	{
		AssetObserverType type;
		AGIS_TRY(type = va[0].as<AssetObserverType>();)
		switch (type)
		{
		case AssetObserverType::COL_ROL_MEAN: {
			if (va.size() != 3) AGIS_THROW("invalid number of arguments");
			std::string col;
			size_t window;
			AGIS_TRY(col = va[1].as<std::string>();)
			AGIS_TRY(window = va[2].as<size_t>();)
			exchange_add_observer(
				exchange,
				create_roll_col_observer,
				AssetObserverType::COL_ROL_MEAN,
				col,
				window
			);
			break;
		}
		default:
			AGIS_THROW("invalid asset observer type");
		}
		return AgisResult<bool>(true);
	};

	// Bind the Exchange class with no constructors.
	lua.new_usertype<Exchange>("Exchange",
		sol::no_constructor,
		"get_exchange_id" , &Exchange::get_exchange_id,
		"add_observer", exchange_add_observer_lambda
	);

	// Bind the AgisStrategy class with no constructors.
	lua.new_usertype<AgisStrategy>("AgisStrategy",
		sol::no_constructor,
		"set_net_leverage_trace", &AgisStrategy::set_net_leverage_trace,
		"get_exchange", &AgisStrategy::get_exchange
	);


	// Bind the AgisLuaStrategy class with no constructors.
	lua.new_usertype<AgisLuaStrategy>("AgisLuaStrategy",
		sol::no_constructor,
		"set_allocation_node", &AgisLuaStrategy::set_allocation_node,
		"set_net_leverage_trace", &AgisLuaStrategy::set_net_leverage_trace,
		"set_beta_trace", &AgisLuaStrategy::set_beta_trace,
		"set_step_frequency", &AgisLuaStrategy::set_step_frequency,
		"get_exchange", &AgisLuaStrategy::get_exchange,
		"exchange_subscribe", &AgisLuaStrategy::exchange_subscribe
	);
}


//============================================================================
AgisLuaStrategy::AgisLuaStrategy(
	PortfolioPtr const& portfolio_,
	std::string const& strategy_id, 
	double allocation,
	std::string const& script_
) : AgisStrategy(strategy_id, portfolio_, allocation)
{
	this->strategy_type = AgisStrategyType::LUAJIT;
	this->script = script_;
}


//============================================================================
AgisLuaStrategy::AgisLuaStrategy(
	PortfolioPtr const& portfolio_,
	std::string const& strategy_id,
	double allocation,
	fs::path const& script_path_,
	bool lazy_load
)
	: AgisStrategy(strategy_id, portfolio_, allocation)
{
	this->strategy_type = AgisStrategyType::LUAJIT;
	if (!fs::exists(script_path_)) {
		AGIS_THROW("invalid lua strategy script path: " + script_path_.string());
	}
	this->load_script_txt(script_path_);
}


//============================================================================
AgisLuaStrategy::~AgisLuaStrategy()
{
	this->lua_ptr = nullptr;
}


//============================================================================
AGIS_API void AgisLuaStrategy::load_script_txt(fs::path script_path_)
{
	// force garbage collection to remove any references to the old strategy logic
	auto& lua = *lua_ptr;
	lua[this->get_strategy_id()] = sol::lua_nil;
	lua_ptr->collect_garbage();

	// reset allocation node
	this->allocation_node = nullptr;

	// Open the file for reading
	std::ifstream fileStream(script_path_);

	// Check if the file was opened successfully
	if (!fileStream.is_open()) {
		AGIS_THROW("Failed to open file: " + script_path_.string());
	}

	// Read the contents of the file into a string
	this->script = "";
	std::string line;
	while (std::getline(fileStream, line)) {
		this->script += line + "\n"; // Add newline character if needed
	}

	fileStream.close();
	this->script_path = script_path_;
}


//============================================================================
void AgisLuaStrategy::call_lua(const std::string& function_name) {
	// Get the Lua function
	sol::function lua_function = (*this->lua_ptr)[this->get_strategy_id() + function_name];

	// Check if the Lua function is valid
	if (!lua_function.valid() || lua_function == sol::lua_nil) {
		AGIS_THROW("Invalid lua function call: " + this->get_strategy_id() + function_name);
	}

	// Call the Lua function, passing 'this' pointer
	try {
		auto res = lua_function(this);
		if (!res.valid()) {
			sol::error error = res;
			AGIS_THROW(function_name + " failed: " + error.what());
		}
	}
	catch (sol::error& e) {
		AGIS_THROW("Invalid lua function call: " + this->get_strategy_id() + function_name + "\n" + e.what());
	}
}


//============================================================================
void AgisLuaStrategy::next() {
	if (this->allocation_node) {
		auto res = this->allocation_node->execute();
		if (res.is_exception()) {
			throw res.get_exception();
		}
	}
	else {
		AGIS_TRY(this->call_lua("_next"););
	}
}


//============================================================================
void AgisLuaStrategy::reset() {	
	AGIS_TRY(this->call_lua("_reset"););
}


//============================================================================
void AgisLuaStrategy::build() {
	// create new lua table if it doesn't exist, use strategy id as key
	this->lua_table =(*this->lua_ptr)[this->get_strategy_id()].get_or_create<sol::table>();

	// load in the script if it hasn't been loaded yet
	if (!this->loaded) {
		try {
			// Execute the Lua script
			lua_ptr->script(this->script);
		}
		catch (sol::error& e) {
			AGIS_THROW("invalid lua strategy script: " + this->get_strategy_id() + "\n" + e.what());
		}
	}
	
	// attempt to run the build method
	AGIS_TRY(this->call_lua("_build");)

	// if build method generated an allocation node, set it
	if (this->allocation_node) {
		this->warmup = this->allocation_node->get_warmup();
	}
	this->loaded = true;
}

AGIS_API void AgisLuaStrategy::to_json(json& j) const
{
	if (script_path.has_value()) {
		j["lua_script_path"] = this->script_path.value().string();
	}
	AgisStrategy::to_json(j);
}


//============================================================================
AGIS_API std::string AgisLuaStrategy::get_script_template(std::string const& strategy_id)
{
	std::string script = R"(
function {STRATEGY_ID}_next(strategy)
	-- Custom Lua implementation of next()
end

function {STRATEGY_ID}_reset(strategy)   
 -- Custom Lua implementation of reset()
end

function {STRATEGY_ID}_build(strategy)
    -- Custom Lua implementation of build() 
end
)";
	str_replace_all(script, "{STRATEGY_ID}", strategy_id);
	return script;
}

#endif // USE_LUAJIT