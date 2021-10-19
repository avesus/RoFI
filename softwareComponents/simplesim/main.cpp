#include <iostream>

#include <dimcli/cli.h>

#include "communication.hpp"
#include "configuration/universalModule.hpp"
#include "controllers.hpp"
#include "gazebo_master.hpp"
#include "simulation.hpp"


rofi::configuration::Rofibot readConfigurationFromFile( const std::string & cfgFileName )
{
    auto inputCfgFile = std::ifstream( cfgFileName );
    if ( !inputCfgFile.is_open() ) {
        throw std::runtime_error( "Cannot open file '" + cfgFileName + "'" );
    }

    auto configuration = rofi::configuration::readOldConfigurationFormat( inputCfgFile );
    auto modules = configuration.modules();
    if ( modules.size() != 0 ) {
        const auto & firstModule = modules.begin()->module;
        assert( firstModule.get() );
        assert( !firstModule->bodies().empty() );
        rofi::configuration::connect< rofi::configuration::RigidJoint >( firstModule->bodies()
                                                                                 .front(),
                                                                         Vector( { 0, 0, 0 } ),
                                                                         identity );
    }
    if ( auto [ ok, str ] = configuration.isValid( rofi::configuration::SimpleColision() ); !ok ) {
        throw std::runtime_error( str );
    }
    return configuration;
}


[[nodiscard]] rofi::simplesim::Controller runSimplesim( rofi::configuration::Rofibot configuration )
{
    using namespace rofi::simplesim;

    auto gzMaster = startGazeboMaster();

    auto simulation = std::make_shared< Simulation >( std::move( configuration ) );
    auto communication = std::make_shared< Communication >( simulation->commandHandler() );

    return Controller::runRofiController( std::move( simulation ), std::move( communication ) );
}


int main( int argc, char * argv[] )
{
    Dim::Cli cli;
    auto & inputCfgFileName = cli.opt< std::string >( "<input_cfg_file>" )
                                      .desc( "Input configuration file" );

    if ( !cli.parse( argc, argv ) ) {
        return cli.printError( std::cerr );
    }

    std::cout << "Reading configuration from file" << std::endl;
    auto configuration = readConfigurationFromFile( *inputCfgFileName );

    std::cout << "Starting simplesim..." << std::endl;
    auto controller = runSimplesim( std::move( configuration ) );
    std::cout << "Simulating..." << std::endl;
    controller.wait();
}
