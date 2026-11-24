/**
 * Module for interactive with Flipper Video Game Module (VGM)
 * @version Available with JS feature `vgm`
 * @module
 */

/**
 * @brief Get current VGM pitch, or undefined if VGM not present
 */
export declare function getPitch(): number | undefined;

/**
 * @brief Get current VGM roll, or undefined if VGM not present
 */
export declare function getRoll(): number | undefined;

/**
 * @brief Get current VGM yaw, or undefined if VGM not present
 */
export declare function getYaw(): number | undefined;

/**
 * @brief Wait until yaw changed by specified amount
 * 
 * Returns how much the yaw changed from initial value if it exceeded the
 * specified yaw angle, or returns 0 if it was not exceeded within the timeout
 * Or returns undefined if VGM is not present
 * 
 * @param angle How much the yaw needs to change
 * @param timeout Maximum time in milliseconds to wait for specified yaw change, default 3000ms
 */
export declare function deltaYaw(angle: number, timeout?: number): number | undefined;

/**
 * @brief Whether a Video Game Module is attached and its expansion link is up
 *
 * This is what the `gpio*` functions below need. It doesn't depend on the IMU,
 * unlike `getPitch()` and friends.
 * @version Flipware only
 */
export declare function present(): boolean;

/**
 * VGM breakout pin, as an RP2040 GPIO number: 16, 17 or 21 to 29.
 * Any other pin is refused by the module and throws.
 * GPIO 21, 24 and 25 also drive the VGM's RGB LED, so the module may
 * overwrite them when its link state changes.
 */
export type VgmPin = number;

/**
 * - `"in"`: input, no pull
 * - `"out"`: push-pull output, starts low
 * - `"up"`: input with pull-up
 * - `"down"`: input with pull-down
 */
export type VgmGpioMode = "in" | "out" | "up" | "down";

/**
 * @brief Set a VGM breakout pin's direction and pull
 *
 * Throws if the VGM doesn't answer or refuses the pin.
 * @version Flipware only
 */
export declare function gpioMode(pin: VgmPin, mode: VgmGpioMode): void;

/**
 * @brief Drive a VGM breakout pin that is in `"out"` mode
 *
 * Throws if the VGM doesn't answer, refuses the pin, or the pin isn't an output.
 * @version Flipware only
 */
export declare function gpioWrite(pin: VgmPin, level: boolean): void;

/**
 * @brief Read the level of a VGM breakout pin
 *
 * Throws if the VGM doesn't answer or refuses the pin.
 * @version Flipware only
 */
export declare function gpioRead(pin: VgmPin): boolean;

/**
 * @brief Read a VGM ADC pin (26 to 29), in millivolts (0 to 3300)
 *
 * Puts the pin in analog mode; call `gpioMode()` to use it as digital again.
 * Throws if the VGM doesn't answer or the pin has no ADC.
 * @version Flipware only
 */
export declare function gpioAdc(pin: VgmPin): number;
