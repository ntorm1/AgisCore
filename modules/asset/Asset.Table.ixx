module;

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

export module Asset:Table;

import :Core;
import :Base;


export namespace Agis {

class Asset;
typedef std::shared_ptr<Asset> AssetPtr;

std::expected<bool, AgisException> build_futures_tables(Exchange* exchange);
std::expected<bool, AgisException> build_asset_tables(Exchange* exchange);


class AssetTable {
public:
	AssetTable(Exchange* exchange) : _exchange(exchange) {}

	typedef std::deque<AssetPtr>::iterator iterator;
	typedef std::deque<AssetPtr>::const_iterator const_iterator;

	virtual std::string const& name() const = 0;
	virtual std::expected<bool, AgisException> build() = 0;

    iterator begin() {
        return _tradeable.begin();
    }

    iterator end() {
        return _tradeable.end();
    }

	const_iterator begin() const {
		return _tradeable.begin();
	}

	const_iterator end() const {
		return _tradeable.end();
	}

protected:
	std::deque<AssetPtr> _tradeable;
	std::deque<AssetPtr> _out_of_bounds;
	Exchange* _exchange;

private:


};

}