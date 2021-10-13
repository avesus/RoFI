#pragma once

#include <optional>
#include <span>
#include <variant>
#include <vector>

#include "configuration/rofibot.hpp"

#include "connectorState.pb.h"


namespace rofi::simplesim
{
using ModuleId = rofi::configuration::ModuleId;

class JointInnerState
{
public:
    struct PositionControl
    {
    public:
        float position = 0.f;
        float speed = 0.f;
    };
    struct VelocityControl
    {
    public:
        float velocity = 0.f;
    };
    using Control = std::variant< VelocityControl, PositionControl >;

    Control control() const
    {
        return _control;
    }

    void setVelocityControl( VelocityControl velocityControl )
    {
        _control = { velocityControl };
    }
    void setPositionControl( PositionControl positionControl )
    {
        assert( positionControl.speed >= 0.f );
        _control = { positionControl };
    }

private:
    Control _control = { VelocityControl{ .velocity = 0.f } };
};

class ConnectorInnerState
{
public:
    using ConnectorState = rofi::messages::ConnectorState;

    struct OtherConnector
    {
    public:
        OtherConnector( ModuleId moduleId, int connector, ConnectorState::Orientation orientation )
                : moduleId( moduleId ), connector( connector ), orientation( orientation )
        {}

        ModuleId moduleId = {};
        int connector = {};
        ConnectorState::Orientation orientation = {};
    };

public:
    void extend()
    {
        _position = true;
    }
    void retract()
    {
        _position = false;
        resetConnectedTo();
    }
    bool position() const
    {
        return _position;
    }

    void setConnectedTo( ModuleId moduleId, int connector, ConnectorState::Orientation orientation )
    {
        _connectedTo = { moduleId, connector, orientation };
    }
    void resetConnectedTo()
    {
        _connectedTo.reset();
    }
    const std::optional< OtherConnector > & connectedTo() const
    {
        return _connectedTo;
    }

    bool internal() const
    {
        return _internal;
    }
    bool & internal()
    {
        return _internal;
    }
    bool external() const
    {
        return _external;
    }
    bool & external()
    {
        return _external;
    }

    ConnectorState connectorState() const
    {
        ConnectorState state;
        state.set_position( position() );
        state.set_internal( internal() );
        state.set_external( external() );
        state.set_connected( connectedTo().has_value() );
        if ( connectedTo().has_value() ) {
            state.set_orientation( connectedTo()->orientation );
        }
        return state;
    }

private:
    bool _position = false;
    bool _internal = false;
    bool _external = false;
    std::optional< OtherConnector > _connectedTo;
};

class ModuleInnerState
{
public:
    ModuleInnerState( int jointsCount, int connectorsCount )
            : _joints( jointsCount ), _connectors( connectorsCount )
    {
        assert( jointsCount >= 0 );
        assert( connectorsCount >= 0 );
    }

    ModuleInnerState( const ModuleInnerState & ) = default;
    ModuleInnerState & operator=( const ModuleInnerState & ) = default;
    ModuleInnerState( ModuleInnerState && ) = default;
    ModuleInnerState & operator=( ModuleInnerState && ) = default;

    std::span< const JointInnerState > joints() const
    {
        return _joints;
    }
    std::span< JointInnerState > joints()
    {
        return _joints;
    }

    std::span< const ConnectorInnerState > connectors() const
    {
        return _connectors;
    }
    std::span< ConnectorInnerState > connectors()
    {
        return _connectors;
    }

private:
    std::vector< JointInnerState > _joints;
    std::vector< ConnectorInnerState > _connectors;
};

} // namespace rofi::simplesim
