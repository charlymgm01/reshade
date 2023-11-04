/*
 * Copyright (C) 2014 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "d3d12_impl_device.hpp"
#include "d3d12_impl_command_queue.hpp"
#include "d3d12_impl_swapchain.hpp"
#include "d3d12_impl_type_convert.hpp"
#include "addon_manager.hpp"
#include <CoreWindow.h>

reshade::d3d12::swapchain_impl::swapchain_impl(device_impl *device, command_queue_impl *queue, IDXGISwapChain3 *swapchain) :
	api_object_impl(swapchain),
	_device_impl(device)
{
	create_effect_runtime(this, queue);

	if (_orig != nullptr)
		on_init();
}
reshade::d3d12::swapchain_impl::~swapchain_impl()
{
	on_reset();

	destroy_effect_runtime(this);
}

reshade::api::device *reshade::d3d12::swapchain_impl::get_device()
{
	return _device_impl;
}

void *reshade::d3d12::swapchain_impl::get_hwnd() const
{
	assert(_orig != nullptr);

	if (HWND hwnd = nullptr;
		SUCCEEDED(_orig->GetHwnd(&hwnd)))
		return hwnd;
	else if (com_ptr<ICoreWindowInterop> window_interop; // Get window handle of the core window
		SUCCEEDED(_orig->GetCoreWindow(IID_PPV_ARGS(&window_interop))) && SUCCEEDED(window_interop->get_WindowHandle(&hwnd)))
		return hwnd;

	DXGI_SWAP_CHAIN_DESC swap_desc = {};
	_orig->GetDesc(&swap_desc);

	return swap_desc.OutputWindow;
}

reshade::api::resource reshade::d3d12::swapchain_impl::get_back_buffer(uint32_t index)
{
	return to_handle(_back_buffers[index].get());
}

uint32_t reshade::d3d12::swapchain_impl::get_back_buffer_count() const
{
	return static_cast<uint32_t>(_back_buffers.size());
}
uint32_t reshade::d3d12::swapchain_impl::get_current_back_buffer_index() const
{
	assert(_orig != nullptr);

	return _orig->GetCurrentBackBufferIndex();
}

bool reshade::d3d12::swapchain_impl::check_color_space_support(api::color_space color_space) const
{
	UINT support;
	return color_space != api::color_space::unknown && SUCCEEDED(_orig->CheckColorSpaceSupport(convert_color_space(color_space), &support)) && (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) != 0;
}

void reshade::d3d12::swapchain_impl::set_color_space(DXGI_COLOR_SPACE_TYPE type)
{
	_back_buffer_color_space = convert_color_space(type);
}

void reshade::d3d12::swapchain_impl::on_init()
{
	assert(_orig != nullptr);

	DXGI_SWAP_CHAIN_DESC swap_desc;
	// Get description from 'IDXGISwapChain' interface, since later versions are slightly different
	if (FAILED(_orig->GetDesc(&swap_desc)))
		return;

	// Get back buffer textures
	_back_buffers.resize(swap_desc.BufferCount);
	for (UINT i = 0; i < swap_desc.BufferCount; ++i)
	{
		if (FAILED(_orig->GetBuffer(i, IID_PPV_ARGS(&_back_buffers[i]))))
			return;
		assert(_back_buffers[i] != nullptr);
	}

	assert(swap_desc.BufferUsage & DXGI_USAGE_RENDER_TARGET_OUTPUT);

#if RESHADE_ADDON
	invoke_addon_event<addon_event::init_swapchain>(this);
#endif

	init_effect_runtime(this);
}
void reshade::d3d12::swapchain_impl::on_reset()
{
	if (_back_buffers.empty())
		return;

	reset_effect_runtime(this);

#if RESHADE_ADDON
	invoke_addon_event<addon_event::destroy_swapchain>(this);
#endif

	_back_buffers.clear();
}

reshade::d3d12::swapchain_d3d12on7_impl::swapchain_d3d12on7_impl(device_impl *device, command_queue_impl *queue) : swapchain_impl(device, queue, nullptr)
{
	// Default to three back buffers for d3d12on7
	_back_buffers.resize(3);
}

bool reshade::d3d12::swapchain_d3d12on7_impl::on_present(ID3D12Resource *source, HWND hwnd)
{
	assert(source != nullptr);

	_hwnd = hwnd;
	_swap_index = (_swap_index + 1) % static_cast<UINT>(_back_buffers.size());

	// Update source texture render target view
	if (_back_buffers[_swap_index] != source)
	{
#if RESHADE_ADDON
		if (_back_buffers[0] != nullptr)
			invoke_addon_event<addon_event::destroy_swapchain>(this);
#endif

		// Reduce number of back buffers if less are used than predicted
		if (const auto it = std::find(_back_buffers.begin(), _back_buffers.end(), source); it != _back_buffers.end())
			_back_buffers.erase(it);
		else
			_back_buffers[_swap_index] = source;

		// Do not initialize before all back buffers have been set
		// The first to be set is at index 1 due to the addition above, so it is sufficient to check the last to be set, which will be at index 0
#if RESHADE_ADDON
		if (_back_buffers[0] != nullptr)
			invoke_addon_event<addon_event::init_swapchain>(this);
#endif
	}

	return _back_buffers[0] != nullptr;
}
