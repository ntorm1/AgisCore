module;

#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif
#include <expected>
#include <memory>
#include <veque.hpp>

#include "AgisException.h"

export module Asset:Table;

import :Core;
import :Base;


export namespace Agis {

class Asset;
typedef std::shared_ptr<Asset> AssetPtr;


class AssetTable {
public:
	AssetTable(Exchange* exchange) : _exchange(exchange) {}

	typedef veque::veque<AssetPtr>::iterator iterator;
	typedef veque::veque<AssetPtr>::const_iterator const_iterator;

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

private:
	veque::veque<AssetPtr> _tradeable;
	veque::veque<AssetPtr> _out_of_bounds;
	Exchange* _exchange;
};

}