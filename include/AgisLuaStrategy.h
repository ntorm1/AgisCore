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

//============================================================================
void init_lua_interface(sol::state* lua);


//============================================================================
/**
 * Convert a Lua sequence into a C++ vector
 * Throw exception on errors or wrong types
 */
template <typename T>
std::vector<T> convert_sequence(sol::table t)
{
	std::size_t sz = t.size();
	std::vector<T> res(sz);
	for (int i = 1; i <= sz; i++) {
		res[i - 1] = t[i];
	}
	return res;
}


//============================================================================
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
	AGIS_API std::expected<rapidjson::Document, AgisException> to_json() const override;
	
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