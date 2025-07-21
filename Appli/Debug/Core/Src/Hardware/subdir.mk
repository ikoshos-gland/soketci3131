################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/Hardware/ads1299_driver.c 

OBJS += \
./Core/Src/Hardware/ads1299_driver.o 

C_DEPS += \
./Core/Src/Hardware/ads1299_driver.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/Hardware/%.o Core/Src/Hardware/%.su Core/Src/Hardware/%.cyclo: ../Core/Src/Hardware/%.c Core/Src/Hardware/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32H7S3xx -DUSE_NUCLEO_64 -c -I../Core/Inc -I../../Drivers/STM32H7RSxx_HAL_Driver/Inc -I../../Drivers/STM32H7RSxx_HAL_Driver/Inc/Legacy -I../../Drivers/BSP/STM32H7RSxx_Nucleo -I../../Drivers/CMSIS/Device/ST/STM32H7RSxx/Include -I../../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src-2f-Hardware

clean-Core-2f-Src-2f-Hardware:
	-$(RM) ./Core/Src/Hardware/ads1299_driver.cyclo ./Core/Src/Hardware/ads1299_driver.d ./Core/Src/Hardware/ads1299_driver.o ./Core/Src/Hardware/ads1299_driver.su

.PHONY: clean-Core-2f-Src-2f-Hardware

