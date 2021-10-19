#pragma once
#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <shared_mutex>
#include <string>
#include <vector>

#include <gazebo/transport/transport.hh>

#include "atoms/guarded.hpp"
#include "command_handler.hpp"
#include "distributor.hpp"
#include "locked_module_communication.hpp"

#include <rofiCmd.pb.h>


namespace rofi::simplesim
{
class ModulesCommunication
{
public:
    using RofiId = CommandHandler::RofiId;

    ModulesCommunication( std::shared_ptr< CommandHandler > commandHandler,
                          gazebo::transport::NodePtr node )
            : _commandHandler( std::move( commandHandler ) )
            , _node( std::move( node ) )
            , _freeModules( this->_commandHandler->getModuleIds() )
            , _distributor( *this->_node, *this )
    {
        assert( _node );
        assert( _commandHandler );
    }

    ModulesCommunication( const ModulesCommunication & ) = delete;
    ModulesCommunication & operator=( const ModulesCommunication & ) = delete;

    // Returns true if the insertion was succesful
    // Returns false if the rofiId was already registered
    bool addNewRofi( RofiId rofiId );

    std::optional< RofiId > lockFreeRofi();
    bool tryLockRofi( RofiId rofiId );
    bool unlockRofi( RofiId rofiId );

    std::optional< std::string > getTopic( RofiId rofiId ) const;
    bool isLocked( RofiId rofiId ) const;

    template < typename F >
    void forEachLockedModule( F && function ) const
    {
        std::shared_lock< std::shared_mutex > lock( _modulesMutex );

        for ( auto & [ rofiId, moduleComm ] : _lockedModules ) {
            function( rofiId, moduleComm.topic( *_node ) );
        }
    }

    template < typename F >
    void forEachFreeModule( F && function ) const
    {
        std::shared_lock< std::shared_mutex > lock( _modulesMutex );

        for ( auto rofiId : _freeModules ) {
            function( rofiId );
        }
    }

    template < typename ResponsesContainer >
    void sendRofiResponses( ResponsesContainer && responses )
    {
        std::shared_lock< std::shared_mutex > lock1( _modulesMutex );

        for ( auto resp : responses ) {
            auto it = _lockedModules.find( resp.rofiid() );
            it->second.sendResponse( std::move( resp ) );
        }
    }

private:
    std::atomic_int topicNameCounter = 0;
    std::string getNewTopicName()
    {
        return "rofi_uid_" + std::to_string( topicNameCounter++ );
    }


    LockedModuleCommunication getNewLockedModule( RofiId rofiId )
    {
        assert( _commandHandler );
        assert( _node );
        return LockedModuleCommunication( *_commandHandler, *_node, getNewTopicName(), rofiId );
    }

private:
    std::shared_ptr< CommandHandler > _commandHandler;

    gazebo::transport::NodePtr _node;

    mutable std::shared_mutex _modulesMutex;
    std::set< RofiId > _freeModules;
    std::map< RofiId, LockedModuleCommunication > _lockedModules;

    Distributor _distributor;
};

} // namespace rofi::simplesim
