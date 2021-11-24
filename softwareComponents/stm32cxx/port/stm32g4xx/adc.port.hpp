#pragma once

#include <stm32g4xx_ll_adc.h>
#include <stm32g4xx_hal.h>
#include <stm32g4xx_ll_bus.h>
#include <system/assert.hpp>

namespace detail {

template < typename Self >
class Adc {
public:
    Self& self() {
        return *static_cast< Self * >( this );
    }
    const Self& self() const {
        return *static_cast< Self * >( this );
    }

    static LL_ADC_InitTypeDef _defaultInitStruct() {
        // TBA
        LL_ADC_InitTypeDef initStruct{};
        // initStruct.Clock = LL_ADC_CLOCK_ASYNC;
        // initStruct.Resolution = LL_ADC_RESOLUTION_12B;
        // initStruct.DataAlignment = LL_ADC_DATA_ALIGN_RIGHT;
        // initStruct.LowPowerMode = LL_ADC_LP_MODE_NONE;
        assert(false);
        return initStruct;
    }

    static LL_ADC_REG_InitTypeDef _defaultRegInitStruct() {
        LL_ADC_REG_InitTypeDef regInitStruct{};
        // TBA
        // regInitStruct.TriggerSource = LL_ADC_REG_TRIG_SOFTWARE;
        // regInitStruct.SequencerLength = LL_ADC_REG_SEQ_SCAN_DISABLE;
        // regInitStruct.SequencerDiscont = LL_ADC_REG_SEQ_DISCONT_DISABLE;
        // regInitStruct.ContinuousMode = LL_ADC_REG_CONV_SINGLE;
        // regInitStruct.DMATransfer = LL_ADC_REG_DMA_TRANSFER_LIMITED;
        // regInitStruct.Overrun = LL_ADC_REG_OVR_DATA_PRESERVED;
        assert(false);
        return regInitStruct;
    }

    void enableClock() {
        LL_AHB2_GRP1_EnableClock( LL_AHB2_GRP1_PERIPH_ADC12 );
    }

    void setupDefaultScanDirection() {
        // TBA
        assert( false );
    }

    void setupDefaultSampling() {
        LL_ADC_SetSamplingTimeCommonChannels( self()._periph,
            LL_ADC_SAMPLINGTIME_2CYCLES_5,
            LL_ADC_SAMPLINGTIME_640CYCLES_5 );
    }

    void calibrate() {
        assert( !LL_ADC_IsEnabled( self()._periph ) &&
            "Cannot calibrate on enabled ADC" );
        LL_ADC_StartCalibration( self()._periph );
        while( LL_ADC_IsCalibrationOnGoing( self()._periph ) );
        // At least LL_ADC_DELAY_CALIB_ENABLE_ADC_CYCLES must pass
        // ToDo: We act safe & slow here, update if necessary
        HAL_Delay( 1 );
    }

    void disableConversionInterrupts() {
        LL_ADC_DisableIT_EOC( self()._periph );
        LL_ADC_DisableIT_EOS( self()._periph );
    }

    void platformPostConfiguration() {
        // No post configuration for this platform
    }

    void startSingleConversion( int channelMask ) {
        LL_ADC_REG_SetSequencerChannels( self()._peripheral , channelMask );
        LL_ADC_REG_StartConversion( self()._peripheral );
    }

    void waitForConversion() {
        while ( LL_ADC_REG_IsConversionOngoing( adc ) );
    }
};

} // namespace detail
