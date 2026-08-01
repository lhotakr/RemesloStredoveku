#pragma once

#include <optional>
#include <string>
#include <utility>

namespace interior
{
    enum class LocationLayer
    {
        Campaign2D,
        Interior25D
    };

    struct LocationRequest
    {
        LocationLayer layer = LocationLayer::Campaign2D;
        std::string locationId;
        std::string spawnId;
    };

    // Small dependency-free queue used later by Game to move between the
    // existing 2D Campaign and selected 2.5D locations without giving either
    // renderer ownership of the player, inventory or save-game state.
    class LocationRouter
    {
    public:
        void requestCampaignMap(std::string mapPath, std::string spawnId = {})
        {
            m_pending = LocationRequest{
                LocationLayer::Campaign2D,
                std::move(mapPath),
                std::move(spawnId)
            };
        }

        void requestInterior(std::string locationId, std::string spawnId = {})
        {
            m_pending = LocationRequest{
                LocationLayer::Interior25D,
                std::move(locationId),
                std::move(spawnId)
            };
        }

        bool hasPendingRequest() const { return m_pending.has_value(); }

        std::optional<LocationRequest> consume()
        {
            std::optional<LocationRequest> result = std::move(m_pending);
            m_pending.reset();
            return result;
        }

    private:
        std::optional<LocationRequest> m_pending;
    };
}
