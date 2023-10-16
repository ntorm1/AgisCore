#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif
#include <expected>
#include <memory>
#include <deque>


#include "AgisException.h"

#include "Asset/Asset.Core.h"
#include "Asset/Asset.Base.h"


namespace Agis {

class Derivative;
typedef std::shared_ptr<Derivative> DerivativePtr;

std::expected<bool, AgisException> build_asset_tables(Exchange* exchange);


class AssetTable {
	friend class Exchange;
public:
	typedef std::deque<DerivativePtr>::const_iterator const_iterator;

	AssetTable(Exchange* exchange) : _exchange(exchange) {}
	
	virtual std::string const& name() const = 0;
	std::vector<AssetPtr> all_assets() const noexcept;
	void sort_expirable(std::deque<DerivativePtr> &table) noexcept;

	const_iterator begin() const {
		return _tradeable.begin();
	}

	const_iterator end() const {
		return _tradeable.end();
	}

protected:
	virtual std::expected<bool, AgisException> __build();
	void next();
	void reset();

	std::deque<DerivativePtr> _tradeable;
	std::deque<DerivativePtr> _out_of_bounds;
	Exchange* _exchange;

private:


};

}