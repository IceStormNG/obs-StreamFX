/*
 * Modern effects for a modern Streamer
 * Copyright (C) 2020 Michael Fabian Dirks
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

#include "nvidia-ar-facedetection.hpp"
#include <algorithm>
#include <stdexcept>
#include "obs/gs/gs-helper.hpp"
#include "util/util-logging.hpp"

#ifdef _DEBUG
#define ST_PREFIX "<%s> "
#define D_LOG_ERROR(x, ...) P_LOG_ERROR(ST_PREFIX##x, __FUNCTION_SIG__, __VA_ARGS__)
#define D_LOG_WARNING(x, ...) P_LOG_WARN(ST_PREFIX##x, __FUNCTION_SIG__, __VA_ARGS__)
#define D_LOG_INFO(x, ...) P_LOG_INFO(ST_PREFIX##x, __FUNCTION_SIG__, __VA_ARGS__)
#define D_LOG_DEBUG(x, ...) P_LOG_DEBUG(ST_PREFIX##x, __FUNCTION_SIG__, __VA_ARGS__)
#else
#define ST_PREFIX "<nvidia::ar::facedetection> "
#define D_LOG_ERROR(...) P_LOG_ERROR(ST_PREFIX __VA_ARGS__)
#define D_LOG_WARNING(...) P_LOG_WARN(ST_PREFIX __VA_ARGS__)
#define D_LOG_INFO(...) P_LOG_INFO(ST_PREFIX __VA_ARGS__)
#define D_LOG_DEBUG(...) P_LOG_DEBUG(ST_PREFIX __VA_ARGS__)
#endif

// At the current moment, both FaceDetection and FaceBoxDetection only support 8 faces.
#define ST_MAX_TRACKED_FACES 8

using namespace ::streamfx::nvidia;

streamfx::nvidia::ar::facedetection::~facedetection()
{
	D_LOG_DEBUG("Finalizing... (Addr: 0x%" PRIuPTR ")", this);
}

streamfx::nvidia::ar::facedetection::facedetection()
	: feature(FEATURE_FACE_DETECTION), _input(), _source(), _tmp(), _rects(), _rects_confidence(), _bboxes(),
	  _dirty(true)
{
	D_LOG_DEBUG("Initializing... (Addr: 0x%" PRIuPTR ")", this);

	// Assign CUDA Stream object.
	if (auto err = set(P_NVAR_CONFIG "CUDAStream", _nvcuda->get_stream()); err != cv::result::SUCCESS) {
		throw cv::exception("CUDAStream", err);
	}

	// Prepare initial memory
	_rects.resize(ST_MAX_TRACKED_FACES);
	_rects_confidence.resize(ST_MAX_TRACKED_FACES);

	// Set up initial configuration
	set_tracking_limit(1);

	// Attempt to load the feature.
	if (auto err = feature::load(); err != cv::result::SUCCESS) {
		throw cv::exception("Load", err);
	}
}

std::pair<size_t, size_t> ar::facedetection::tracking_limit_range()
{
	return {1, ST_MAX_TRACKED_FACES};
}

size_t ar::facedetection::tracking_limit()
{
	return _rects.size();
}

void ar::facedetection::set_tracking_limit(size_t v)
{
	// Ensure there is always at least one face being tracked.
	v = std::max<size_t>(v, 1);

	// Resize all data.
	_rects.resize(v);
	_rects_confidence.resize(v);

	// Update bounding boxes structure.
	_bboxes.rects   = _rects.data();
	_bboxes.maximum = static_cast<uint8_t>(v);
	_bboxes.current = 0;

	// Update feature.
	if (auto err = set_object(P_NVAR_OUTPUT "BoundingBoxes", reinterpret_cast<void*>(&_bboxes), sizeof(bounds_t));
		err != cv::result::SUCCESS) {
		throw cv::exception("BoundingBoxes", err);
	}
	if (auto err = set(P_NVAR_OUTPUT "BoundingBoxesConfidence", _rects_confidence); err != cv::result::SUCCESS) {
		throw cv::exception("BoundingBoxesConfidence", err);
	}
	if (auto err = set(P_NVAR_CONFIG "Temporal", (v == 1)); err != cv::result::SUCCESS) {
		throw cv::exception("Temporal", err);
	}

	// Mark effect dirty for reload.
	_dirty = true;
}

void ar::facedetection::process(std::shared_ptr<::streamfx::obs::gs::texture> in)
{
	// Enter Graphics and CUDA context.
	auto gctx = ::streamfx::obs::gs::context();
	auto cctx = _nvcuda->get_context()->enter();

#ifdef ENABLE_PROFILING
	::streamfx::obs::gs::debug_marker profiler1{::streamfx::obs::gs::debug_color_magenta, "NvAR Face Detection"};
#endif

	// Resize if the size or scale was changed.
	resize(in->get_width(), in->get_height());

	// Reload effect if dirty.
	if (_dirty) {
		load();
	}

	{ // Copy parameter to input.
#ifdef ENABLE_PROFILING
		::streamfx::obs::gs::debug_marker profiler1{::streamfx::obs::gs::debug_color_copy, "Copy In -> Input"};
#endif
		gs_copy_texture(_input->get_texture()->get_object(), in->get_object());
	}

	{ // Convert Input to Source format
#ifdef ENABLE_PROFILING
		::streamfx::obs::gs::debug_marker profiler1{::streamfx::obs::gs::debug_color_convert, "Copy Input -> Source"};
#endif
		if (auto res = _nvcv->NvCVImage_Transfer(_input->get_image(), _source->get_image(), 1.f,
												 _nvcuda->get_stream()->get(), _tmp->get_image());
			res != ::streamfx::nvidia::cv::result::SUCCESS) {
			D_LOG_ERROR("Failed to transfer input to processing source due to error: %s",
						_nvcv->NvCV_GetErrorStringFromCode(res));
			throw std::runtime_error("Transfer failed.");
		}
	}

	{ // Run
#ifdef ENABLE_PROFILING
		::streamfx::obs::gs::debug_marker profiler1{::streamfx::obs::gs::debug_color_cache, "Run"};
#endif
		if (auto err = run(); err != cv::result::SUCCESS) {
			throw cv::exception("Run", err);
		}
	}
}

size_t streamfx::nvidia::ar::facedetection::count()
{
	return _bboxes.current;
}

streamfx::nvidia::ar::rect_t const& streamfx::nvidia::ar::facedetection::at(size_t index)
{
	float v;
	return at(index, v);
}

streamfx::nvidia::ar::rect_t const& streamfx::nvidia::ar::facedetection::at(size_t index, float& confidence)
{
	if (_bboxes.current == 0)
		throw std::runtime_error("no tracked faces");
	if (index > _bboxes.current)
		throw std::out_of_range("index too large");
	auto& ref  = _rects.at(index);
	confidence = _rects_confidence.at(index);
	return ref;
}

void ar::facedetection::resize(uint32_t width, uint32_t height)
{
	auto gctx = ::streamfx::obs::gs::context();
	auto cctx = ::streamfx::nvidia::cuda::obs::get()->get_context()->enter();

	if (!_tmp) {
		_tmp = std::make_shared<::streamfx::nvidia::cv::image>(
			width, height, ::streamfx::nvidia::cv::pixel_format::RGBA, ::streamfx::nvidia::cv::component_type::UINT8,
			::streamfx::nvidia::cv::component_layout::PLANAR, ::streamfx::nvidia::cv::memory_location::GPU, 1);
	}

	if (!_input || (width != _input->get_texture()->get_width()) || (height != _input->get_texture()->get_height())) {
		if (_input) {
			_input->resize(width, height);
		} else {
			_input = std::make_shared<::streamfx::nvidia::cv::texture>(width, height, GS_RGBA_UNORM);
		}
		_dirty = true;
	}

	if (!_source || (width != _source->get_image()->width) || (height != _source->get_image()->height)) {
		if (_source) {
			_source->resize(width, height);
		} else {
			_source = std::make_shared<::streamfx::nvidia::cv::image>(
				width, height, ::streamfx::nvidia::cv::pixel_format::BGR, ::streamfx::nvidia::cv::component_type::UINT8,
				::streamfx::nvidia::cv::component_layout::INTERLEAVED, ::streamfx::nvidia::cv::memory_location::GPU, 1);
		}

		if (auto err = set(P_NVAR_INPUT "Image", _source); err != cv::result::SUCCESS) {
			throw cv::exception("Image", err);
		}

		_dirty = true;
	}
}

void streamfx::nvidia::ar::facedetection::load()
{
	auto gctx = ::streamfx::obs::gs::context();
	auto cctx = ::streamfx::nvidia::cuda::obs::get()->get_context()->enter();

	// Assign CUDA Stream object.
	if (auto err = set(P_NVAR_CONFIG "CUDAStream", _nvcuda->get_stream()); err != cv::result::SUCCESS) {
		throw cv::exception("CUDAStream", err);
	}

	// Attempt to load the feature.
	if (auto err = feature::load(); err != cv::result::SUCCESS) {
		throw cv::exception("Load", err);
	}

	_dirty = false;
}
