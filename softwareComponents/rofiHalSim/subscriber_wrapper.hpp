#pragma once

#include <cassert>
#include <functional>
#include <memory>

#include <gazebo/transport/transport.hh>

#include "gazebo_node_handler.hpp"


namespace rofi::hal
{
template < typename Message >
class SubscriberWrapper
{
public:
    SubscriberWrapper( const GazeboNodeHandler & node,
                       const std::string & topic,
                       std::function< void( const Message & ) > callback )
            : _callback( std::move( callback ) )
            , _node( node )
    {
        if ( !_callback )
        {
            throw std::runtime_error( "empty callback" );
        }

        _sub = _node->Subscribe( topic, &SubscriberWrapper::onMsg, this );
    }

    SubscriberWrapper( const SubscriberWrapper & ) = delete;
    SubscriberWrapper & operator=( const SubscriberWrapper & ) = delete;

    ~SubscriberWrapper()
    {
        if ( _sub )
        {
            _sub->Unsubscribe();
        }
    }

private:
    void onMsg( const boost::shared_ptr< const Message > & msg )
    {
        assert( _callback );
        assert( msg );

        auto localMsg = msg;
        assert( localMsg );
        _callback( *localMsg );
    }

    const std::function< void( const Message & ) > _callback;
    GazeboNodeHandler _node;
    gazebo::transport::SubscriberPtr _sub;
};

template < typename Message >
using SubscriberWrapperPtr = std::unique_ptr< SubscriberWrapper< Message > >;

} // namespace rofi::hal
