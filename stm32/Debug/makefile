################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

-include ../makefile.init

RM := rm -rf

# All of the sources participating in the build are defined here
-include sources.mk
-include Drivers/STM32F1xx_HAL_Driver/Src/subdir.mk
-include Core/Startup/subdir.mk
-include Core/Src/subdir.mk
-include objects.mk

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(S_DEPS)),)
-include $(S_DEPS)
endif
ifneq ($(strip $(S_UPPER_DEPS)),)
-include $(S_UPPER_DEPS)
endif
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
endif

-include ../makefile.defs

OPTIONAL_TOOL_DEPS := \
$(wildcard ../makefile.defs) \
$(wildcard ../makefile.init) \
$(wildcard ../makefile.targets) \


BUILD_ARTIFACT_NAME := DAKTNC_DUANTONGHOP
BUILD_ARTIFACT_EXTENSION := elf
BUILD_ARTIFACT_PREFIX :=
BUILD_ARTIFACT := $(BUILD_ARTIFACT_PREFIX)$(BUILD_ARTIFACT_NAME)$(if $(BUILD_ARTIFACT_EXTENSION),.$(BUILD_ARTIFACT_EXTENSION),)

# Add inputs and outputs from these tool invocations to the build variables 
EXECUTABLES += \
DAKTNC_DUANTONGHOP.elf \

MAP_FILES += \
DAKTNC_DUANTONGHOP.map \

SIZE_OUTPUT += \
default.size.stdout \

OBJDUMP_LIST += \
DAKTNC_DUANTONGHOP.list \

OBJCOPY_HEX += \
DAKTNC_DUANTONGHOP.hex \


# All Target
all: main-build

# Main-build Target
main-build: DAKTNC_DUANTONGHOP.elf secondary-outputs

# Tool invocations
DAKTNC_DUANTONGHOP.elf DAKTNC_DUANTONGHOP.map: $(OBJS) $(USER_OBJS) F:\HKI(2026-2027)\TH_DAKTMT_19B\STM32\duan\DAKTNC_DUANTONGHOP\STM32F103C8TX_FLASH.ld makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-gcc -o "DAKTNC_DUANTONGHOP.elf" @"objects.list" $(USER_OBJS) $(LIBS) -mcpu=cortex-m3 -T"F:\HKI(2026-2027)\TH_DAKTMT_19B\STM32\duan\DAKTNC_DUANTONGHOP\STM32F103C8TX_FLASH.ld" --specs=nosys.specs -Wl,-Map="DAKTNC_DUANTONGHOP.map" -Wl,--gc-sections -static --specs=nano.specs -mfloat-abi=soft -mthumb -Wl,--start-group -lc -lm -Wl,--end-group
	@echo 'Finished building target: $@'
	@echo ' '

default.size.stdout: $(EXECUTABLES) makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-size  $(EXECUTABLES)
	@echo 'Finished building: $@'
	@echo ' '

DAKTNC_DUANTONGHOP.list: $(EXECUTABLES) makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-objdump -h -S $(EXECUTABLES) > "DAKTNC_DUANTONGHOP.list"
	@echo 'Finished building: $@'
	@echo ' '

DAKTNC_DUANTONGHOP.hex: $(EXECUTABLES) makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-objcopy  -O ihex $(EXECUTABLES) "DAKTNC_DUANTONGHOP.hex"
	@echo 'Finished building: $@'
	@echo ' '

# Other Targets
clean:
	-$(RM) DAKTNC_DUANTONGHOP.elf DAKTNC_DUANTONGHOP.hex DAKTNC_DUANTONGHOP.list DAKTNC_DUANTONGHOP.map default.size.stdout
	-@echo ' '

secondary-outputs: $(SIZE_OUTPUT) $(OBJDUMP_LIST) $(OBJCOPY_HEX)

fail-specified-linker-script-missing:
	@echo 'Error: Cannot find the specified linker script. Check the linker settings in the build configuration.'
	@exit 2

warn-no-linker-script-specified:
	@echo 'Warning: No linker script specified. Check the linker settings in the build configuration.'

.PHONY: all clean dependents main-build fail-specified-linker-script-missing warn-no-linker-script-specified

-include ../makefile.targets
