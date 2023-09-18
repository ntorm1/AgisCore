#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif
#include "pch.h"

class Asset;
class IncrementalCovariance;

enum class ObeserverType {
    IncrementalCovariance,
    MeanVistor
};

// Observer base class
class AssetObserver {
public:
    virtual ~AssetObserver() {}
    AssetObserver(Asset* asset_) : asset(asset_) {}
    virtual void on_step() = 0;
    virtual void on_reset() = 0;

protected:
    void set_asset_ptr(Asset* asset_) { this->asset = asset_; }

private:
    Asset* asset = nullptr;
};


// Observer Factory
template <typename... Args>
std::shared_ptr<AssetObserver> create_observer(
    ObeserverType type,
    Args&&... args
    ){
    switch (type) {
        case ObeserverType::IncrementalCovariance:
            return std::make_unique<IncrementalCovariance>(
                std::forward<Args>(args)...
            );
        case ObeserverType::MeanVistor:
            throw std::invalid_argument("not impl");
        default:
            throw std::invalid_argument("Invalid ObjectType");
    }
}