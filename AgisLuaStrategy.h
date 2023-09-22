#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif
#ifdef _DEBUG
#define SOL_ALL_SAFETIES_ON 1
#define SOL_EXCEPTIONS_SAFE_PROPAGATION 1
#endif

#include "pch.h"
#ifdef USE_LUAJIT
#define SOL_LUAJIT 1
#include <sol/sol.hpp>

#include <filesystem>
#include "AbstractStrategyTree.h"

namespace fs = std::filesystem;

void init_lua_interface(sol::state* lua);


class AgisLuaStrategy : public AgisStrategy {
public:
	AGIS_API AgisLuaStrategy(
		PortfolioPtr const& portfolio_,
		std::string const& strategy_id,
		double allocation,
		std::string const& script
	);
	AGIS_API AgisLuaStrategy(
		PortfolioPtr const& portfolio_,
		std::string const& strategy_id,
		double allocation,
		fs::path const& script_path,
		bool lazy_load = false
	);

	AGIS_API ~AgisLuaStrategy();

	void next() override;
	void reset() override;
	void build() override;
	AGIS_API void to_json(json& j) const override;
	
	AGIS_API static std::string get_script_template(std::string const& strategy_id);
	AGIS_API void set_lua_ptr(sol::state * lua_ptr_) { lua_ptr = lua_ptr_; }
	
	AGIS_API void load_script_txt(fs::path script_path);
	AGIS_API void set_allocation_node(std::unique_ptr<AbstractStrategyAllocationNode>& allocation_node_) { this->allocation_node = std::move(allocation_node_); }
	AGIS_API void __override_warmup(size_t warmup_) { this->warmup = warmup_; }
protected:
	void call_lua(const std::string& functionName);

private:
	ExchangePtr exchange;
	std::unique_ptr<AbstractStrategyAllocationNode> allocation_node= nullptr;
	sol::state* lua_ptr = nullptr;
	sol::table lua_table;
	std::optional<fs::path> script_path = std::nullopt;
	std::string script;
	bool loaded = false;
};

#endif