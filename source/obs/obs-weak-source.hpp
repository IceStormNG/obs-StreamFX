/*
 * Copyright (C) 2022 Michael Fabian Dirks
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#pragma once
#include "common.hpp"
#include <string_view>
#include "obs-source.hpp"

// ToDo:
// - Is FORCE_INLINE necessary for optimal performance, or can LTO handle this?

namespace streamfx::obs {
	class weak_source {
		obs_weak_source_t* _ref;

		public:
		FORCE_INLINE ~weak_source() noexcept
		{
			if (_ref) {
				obs_weak_source_release(_ref);
			}
		};

		/** Create an empty weak source.
		 *
		 * The weak source will be expired, as it points at nothing.
		 */
		FORCE_INLINE weak_source() : _ref(nullptr){};

		/** Create a new weak reference from an existing pointer.
		 *
		 * @param duplicate If true, will duplicate the pointer instead of taking ownership.
		 */
		FORCE_INLINE weak_source(obs_weak_source_t* source, bool duplicate = true) : _ref(source)
		{
			if (!_ref)
				throw std::invalid_argument("Parameter 'source' does not define a valid source.");
			if (duplicate)
				obs_weak_source_addref(_ref);
		};

		/** Create a new weak reference from an existing hard reference.
		 */
		FORCE_INLINE weak_source(obs_source_t* source)
		{
			_ref = obs_source_get_weak_source(source);
			if (!_ref)
				throw std::invalid_argument("Parameter 'source' does not define a valid source.");
		};

		/** Create a new weak reference from an existing hard reference.
		 */
		FORCE_INLINE weak_source(const ::streamfx::obs::source& source)
		{
			_ref = obs_source_get_weak_source(source.get());
			if (!_ref)
				throw std::invalid_argument("Parameter 'source' does not define a valid source.");
		};

		/** Create a new weak reference for a given source by name.
		 *
		 * Attention: May fail if the name does not exactly match.
		 */
		FORCE_INLINE weak_source(std::string_view name)
		{
			std::shared_ptr<obs_source_t> ref{obs_get_source_by_name(name.data()),
											  [](obs_source_t* v) { obs_source_release(v); }};
			if (!ref) {
				throw std::invalid_argument("Parameter 'name' does not define an valid source.");
			}
			_ref = obs_source_get_weak_source(ref.get());
		};

		public /* Move & Copy Operators */:
		/** Move Constructor
		 * 
		 */
		FORCE_INLINE weak_source(::streamfx::obs::weak_source&& move) noexcept
		{
			_ref      = move._ref;
			move._ref = nullptr;
		};

		/** Copy Constructor
		 * 
		 */
		FORCE_INLINE weak_source(const ::streamfx::obs::weak_source& copy) noexcept
		{
			_ref = copy._ref;
			obs_weak_source_addref(_ref);
		};

		/** Move Assign
		 * 
		 */
		FORCE_INLINE ::streamfx::obs::weak_source& operator=(::streamfx::obs::weak_source&& move) noexcept
		{
			reset();
			if (move._ref) {
				_ref      = move._ref;
				move._ref = nullptr;
			}
			return *this;
		};

		/** Copy Assign
		 * 
		 */
		FORCE_INLINE ::streamfx::obs::weak_source& operator=(const ::streamfx::obs::weak_source& copy) noexcept
		{
			reset();
			if (copy._ref) {
				_ref = copy._ref;
				obs_weak_source_addref(_ref);
			}

			return *this;
		};

		public /* ... */:
		/** Retrieve the underlying pointer for manual manipulation.
		 *
		 * Attention: Ownership remains with the class instance.
		 */
		[[deprecated("Prefer operator*()")]] FORCE_INLINE obs_weak_source_t* get() const noexcept
		{
			return _ref;
		};

		/** Release the ownership of the managed object.
		 *
		 * Same as 'x = nullptr;'.
		 */
		FORCE_INLINE void reset() noexcept
		{
			if (_ref) {
				obs_weak_source_release(_ref);
				_ref = nullptr;
			}
		};

		/** Is the weak reference expired?
		 *
		 * A weak reference is expired when the original object it is pointing at no longer exists.
		 */
		FORCE_INLINE bool expired() const noexcept
		{
			return (!_ref) || (obs_weak_source_expired(_ref));
		};

		/** Try and acquire a hard reference to the source.
		 *
		 * May fail if the reference expired before we successfully acquire it.
		 */
		FORCE_INLINE ::streamfx::obs::source lock() const noexcept
		{
			return {obs_weak_source_get_source(_ref)};
		};

		public /* Type Conversion Operators */:
		FORCE_INLINE operator obs_weak_source_t*() const noexcept
		{
			return _ref;
		};

		FORCE_INLINE obs_weak_source_t* operator*() const noexcept
		{
			return _ref;
		};

		FORCE_INLINE operator bool() const noexcept
		{
			return !expired();
		};

		public /* Comparison Operators */:
		FORCE_INLINE bool operator==(::streamfx::obs::weak_source const& rhs) const noexcept
		{
			return _ref == rhs._ref;
		};

		FORCE_INLINE bool operator<(::streamfx::obs::weak_source const& rhs) const noexcept
		{
			return _ref < rhs._ref;
		};

		FORCE_INLINE bool operator==(obs_weak_source_t* const& rhs) const noexcept
		{
			return _ref == rhs;
		};

		FORCE_INLINE bool operator==(::streamfx::obs::source const& rhs) const noexcept
		{
			return obs_weak_source_references_source(_ref, rhs.get());
		};

		FORCE_INLINE bool operator==(obs_source_t* const& rhs) const noexcept
		{
			return obs_weak_source_references_source(_ref, rhs);
		};
	};
} // namespace streamfx::obs
