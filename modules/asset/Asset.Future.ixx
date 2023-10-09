module;

#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif

#include <string>
#include <memory>
#include <expected>
#include <veque.hpp>

#include "AgisException.h"

export module Asset:Future;

import :Core;
import :Base;


export namespace Agis {

class Asset;
typedef std::shared_ptr<Asset> AssetPtr;



class FutureTable {
public:
	typedef veque::veque<AssetPtr>::iterator iterator;
	typedef veque::veque<AssetPtr>::const_iterator const_iterator;


	FutureTable(std::string contract_id) :
		_contract_id(contract_id)
	{}
	~FutureTable() = default;

    AssetPtr const front_month() const {
		return _tradeable.front();
	}

    iterator begin() {
        return _tradeable.begin();
    }

    const_iterator begin() const {
        return _tradeable.begin();
    }

    iterator end() {
        return _tradeable.end();
    }

    const_iterator end() const {
        return _tradeable.end();
    }

private:
	std::string _contract_id;
	veque::veque<AssetPtr> _tradeable;
	veque::veque<AssetPtr> _out_of_bounds;
};

}