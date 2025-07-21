################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/DSP/filter_chain.c \
../Core/Src/DSP/freq_features.c \
../Core/Src/DSP/missing_dsp_functions.c \
../Core/Src/DSP/time_features.c 

OBJS += \
./Core/Src/DSP/filter_chain.o \
./Core/Src/DSP/freq_features.o \
./Core/Src/DSP/missing_dsp_functions.o \
./Core/Src/DSP/time_features.o 

C_DEPS += \
./Core/Src/DSP/filter_chain.d \
./Core/Src/DSP/freq_features.d \
./Core/Src/DSP/missing_dsp_functions.d \
./Core/Src/DSP/time_features.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/DSP/%.o Core/Src/DSP/%.su Core/Src/DSP/%.cyclo: ../Core/Src/DSP/%.c Core/Src/DSP/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32H7S3xx -DUSE_NUCLEO_64 -c -I../Core/Inc -I../../Drivers/STM32H7RSxx_HAL_Driver/Inc -I../../Drivers/STM32H7RSxx_HAL_Driver/Inc/Legacy -I../../Drivers/BSP/STM32H7RSxx_Nucleo -I../../Drivers/CMSIS/Device/ST/STM32H7RSxx/Include -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src-2f-DSP

clean-Core-2f-Src-2f-DSP:
	-$(RM) ./Core/Src/DSP/filter_chain.cyclo ./Core/Src/DSP/filter_chain.d ./Core/Src/DSP/filter_chain.o ./Core/Src/DSP/filter_chain.su ./Core/Src/DSP/freq_features.cyclo ./Core/Src/DSP/freq_features.d ./Core/Src/DSP/freq_features.o ./Core/Src/DSP/freq_features.su ./Core/Src/DSP/missing_dsp_functions.cyclo ./Core/Src/DSP/missing_dsp_functions.d ./Core/Src/DSP/missing_dsp_functions.o ./Core/Src/DSP/missing_dsp_functions.su ./Core/Src/DSP/time_features.cyclo ./Core/Src/DSP/time_features.d ./Core/Src/DSP/time_features.o ./Core/Src/DSP/time_features.su

.PHONY: clean-Core-2f-Src-2f-DSP

