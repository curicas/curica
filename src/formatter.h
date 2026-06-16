/**
 * @file formatter.h
 * @brief Native JavaScript Code Formatter Interface
 *
 * Exposes a lightweight, ultra-fast code formatter capable of normalizing
 * JavaScript indentation directly in C, bypassing the need for Prettier
 * or external Node.js tooling.
 */
#ifndef FORMATTER_H
#define FORMATTER_H

/**
 * @brief Formats a JavaScript file in-place.
 * 
 * Reads the specified JS file, adjusts indentation and braces to
 * conform to standard formatting conventions, and overwrites the
 * file with the newly formatted string.
 *
 * @param filepath The absolute or relative path to the JS file to be formatted.
 */
void format_javascript_file(const char* filepath);

#endif
