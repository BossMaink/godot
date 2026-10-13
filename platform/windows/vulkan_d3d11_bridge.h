/**************************************************************************/
/*  vulkan_d3d11_bridge.h                                                 */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

// Opt-in Windows GPU presentation bridge for transparent Vulkan windows.
// Vulkan renders directly into imported D3D11 GPU textures. No image readback or CPU pixel conversion.
#pragma once
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <wrl/client.h>
#include <cstring>
#include <vector>
#include "core/io/file_access.h"
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

class VulkanD3D11Bridge {
public:
	static constexpr uint32_t COUNT = 3;
	VkDevice device = VK_NULL_HANDLE;
	VkImage images[COUNT] = {};
	VkDeviceMemory memories[COUNT] = {};
	uint32_t next_image = 0;
	Microsoft::WRL::ComPtr<ID3D11Device> d3d;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> textures[COUNT];
	Microsoft::WRL::ComPtr<IDXGIKeyedMutex> mutexes[COUNT];
	Microsoft::WRL::ComPtr<IDXGISwapChain1> swapchain;
	Microsoft::WRL::ComPtr<IDCompositionDevice> composition;
	Microsoft::WRL::ComPtr<IDCompositionTarget> target;
	Microsoft::WRL::ComPtr<IDCompositionVisual> visual;
	HMODULE dcomp_library = nullptr;

	struct PhaseRow { uint64_t ticks, interval, submit, acquire, buffer, copy, release, flush, present; };
	std::vector<PhaseRow> phases;
	bool trace_enabled = false;
	String trace_path;
	uint64_t previous_present_tick = 0;
	uint64_t trace_qpc = 0, trace_tick = 0, trace_frequency = 0;
	bool reference_present = false;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> cached_backbuffer;
	uint64_t pending_submit_usec = 0;
	uint64_t presents = 0;
	uint64_t wait_usec = 0;
	uint64_t wait_max_usec = 0;

	~VulkanD3D11Bridge() {

		if (trace_enabled && !phases.empty()) {
			Ref<FileAccess> anchor = FileAccess::open(trace_path + "-" + itos((uint64_t)this) + ".json", FileAccess::WRITE);
			if (anchor.is_valid()) { anchor->store_line(vformat("{\"qpc\":%d,\"ticks_us\":%d,\"frequency\":%d}", trace_qpc, trace_tick, trace_frequency)); }
			Ref<FileAccess> file = FileAccess::open(trace_path + "-" + itos((uint64_t)this) + ".csv", FileAccess::WRITE);
			if (file.is_valid()) {
				file->store_line("ticks_us,interval_us,submit_us,acquire_us,buffer_us,copy_us,release_us,flush_us,present_us");
				for (const PhaseRow &r : phases) { file->store_line(vformat("%d,%d,%d,%d,%d,%d,%d,%d,%d", r.ticks,r.interval,r.submit,r.acquire,r.buffer,r.copy,r.release,r.flush,r.present)); }
			}
		}
		if (device) { vkDeviceWaitIdle(device); }
		if (visual) { visual->SetContent(nullptr); }
		if (target) { target->SetRoot(nullptr); }
		if (composition) { composition->Commit(); }
		visual.Reset(); target.Reset(); composition.Reset(); cached_backbuffer.Reset(); swapchain.Reset();
		if (context) { context->Flush(); }
		for (uint32_t i = 0; i < COUNT; ++i) {
			if (images[i]) { vkDestroyImage(device, images[i], nullptr); }
			if (memories[i]) { vkFreeMemory(device, memories[i], nullptr); }
			mutexes[i].Reset(); textures[i].Reset();
		}
		context.Reset(); d3d.Reset();
		if (dcomp_library) { FreeLibrary(dcomp_library); }
	}

	Error initialize(VkPhysicalDevice physical, VkDevice p_device, HWND hwnd, uint32_t width, uint32_t height) {

		device = p_device;
		trace_path = OS::get_singleton()->get_environment("GODOT_VULKAN_D3D11_TRACE");
		trace_enabled = !trace_path.is_empty();
		if (trace_enabled) {
			phases.reserve(131072);
			LARGE_INTEGER qpc, frequency; QueryPerformanceCounter(&qpc); QueryPerformanceFrequency(&frequency);
			trace_qpc = qpc.QuadPart; trace_frequency = frequency.QuadPart; trace_tick = OS::get_singleton()->get_ticks_usec();
		}
		reference_present = OS::get_singleton()->get_environment("GODOT_VULKAN_D3D11_REFERENCE") == "1";
#define BRIDGE_HR(call) do { HRESULT hr = (call); ERR_FAIL_COND_V_MSG(FAILED(hr), ERR_CANT_CREATE, vformat("VULKAN_D3D11 %s HRESULT=0x%08x", #call, (uint32_t)hr)); } while (0)
#define BRIDGE_VK(call) do { VkResult bridge_vk_status = (call); ERR_FAIL_COND_V_MSG(bridge_vk_status != VK_SUCCESS, ERR_CANT_CREATE, vformat("VULKAN_D3D11 %s VkResult=%d", #call, bridge_vk_status)); } while (0)
		VkPhysicalDeviceIDProperties ids = {};
		ids.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
		VkPhysicalDeviceProperties2 properties = {};
		properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		properties.pNext = &ids;
		vkGetPhysicalDeviceProperties2(physical, &properties);
		ERR_FAIL_COND_V_MSG(!ids.deviceLUIDValid, ERR_UNAVAILABLE, "VULKAN_D3D11: no valid adapter LUID; refusing cross-adapter sharing.");
		Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
		BRIDGE_HR(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())));
		Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
		for (UINT i = 0; ; ++i) {
			Microsoft::WRL::ComPtr<IDXGIAdapter1> candidate;
			if (factory->EnumAdapters1(i, candidate.GetAddressOf()) == DXGI_ERROR_NOT_FOUND) { break; }
			DXGI_ADAPTER_DESC1 description = {};
			BRIDGE_HR(candidate->GetDesc1(&description));
			if (memcmp(&description.AdapterLuid, ids.deviceLUID, VK_LUID_SIZE) == 0) { adapter = candidate; break; }
		}
		ERR_FAIL_NULL_V_MSG(adapter.Get(), ERR_UNAVAILABLE, "VULKAN_D3D11: matching adapter missing.");
		D3D_FEATURE_LEVEL requested = D3D_FEATURE_LEVEL_11_0;
		D3D_FEATURE_LEVEL actual;
		BRIDGE_HR(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
				&requested, 1, D3D11_SDK_VERSION, d3d.GetAddressOf(), &actual, context.GetAddressOf()));
		DXGI_SWAP_CHAIN_DESC1 desc = {};
		desc.Width = width; desc.Height = height;
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.BufferCount = COUNT;
		desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
		desc.Scaling = DXGI_SCALING_STRETCH;
		desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
		BRIDGE_HR(factory->CreateSwapChainForComposition(d3d.Get(), &desc, nullptr, swapchain.GetAddressOf()));
		if (!reference_present) { BRIDGE_HR(swapchain->GetBuffer(0, IID_PPV_ARGS(cached_backbuffer.GetAddressOf()))); }
		BRIDGE_HR(factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES));
		dcomp_library = LoadLibraryW(L"dcomp.dll");
		ERR_FAIL_NULL_V(dcomp_library, ERR_UNAVAILABLE);
		using CreateComposition = HRESULT(WINAPI *)(IDXGIDevice *, REFIID, void **);
		auto create_composition = (CreateComposition)(void *)GetProcAddress(dcomp_library, "DCompositionCreateDevice");
		ERR_FAIL_NULL_V(create_composition, ERR_UNAVAILABLE);
		Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
		BRIDGE_HR(d3d.As(&dxgi_device));
		BRIDGE_HR(create_composition(dxgi_device.Get(), IID_PPV_ARGS(composition.GetAddressOf())));
		BRIDGE_HR(composition->CreateTargetForHwnd(hwnd, TRUE, target.GetAddressOf()));
		BRIDGE_HR(composition->CreateVisual(visual.GetAddressOf()));
		BRIDGE_HR(visual->SetContent(swapchain.Get()));
		BRIDGE_HR(target->SetRoot(visual.Get()));
		BRIDGE_HR(composition->Commit());

		VkPhysicalDeviceExternalImageFormatInfo external = {};
		external.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO;
		external.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
		VkPhysicalDeviceImageFormatInfo2 query = {};
		query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
		query.pNext = &external;
		query.format = VK_FORMAT_B8G8R8A8_UNORM;
		query.type = VK_IMAGE_TYPE_2D;
		query.tiling = VK_IMAGE_TILING_OPTIMAL;
		query.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		VkExternalImageFormatProperties supported = {};
		supported.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES;
		VkImageFormatProperties2 result = {};
		result.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
		result.pNext = &supported;
		BRIDGE_VK(vkGetPhysicalDeviceImageFormatProperties2(physical, &query, &result));
		ERR_FAIL_COND_V_MSG(!(supported.externalMemoryProperties.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT), ERR_UNAVAILABLE, "VULKAN_D3D11: BGRA8 D3D11 texture import unsupported.");
		auto get_handle_properties = (PFN_vkGetMemoryWin32HandlePropertiesKHR)vkGetDeviceProcAddr(device, "vkGetMemoryWin32HandlePropertiesKHR");
		ERR_FAIL_NULL_V(get_handle_properties, ERR_UNAVAILABLE);
		for (uint32_t i = 0; i < COUNT; ++i) {
			D3D11_TEXTURE2D_DESC td = {};
			td.Width = width; td.Height = height; td.MipLevels = 1; td.ArraySize = 1;
			td.Format = desc.Format; td.SampleDesc.Count = 1;
			td.Usage = D3D11_USAGE_DEFAULT;
			td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
			BRIDGE_HR(d3d->CreateTexture2D(&td, nullptr, textures[i].GetAddressOf()));
			BRIDGE_HR(textures[i].As(&mutexes[i]));
			Microsoft::WRL::ComPtr<IDXGIResource1> resource;
			BRIDGE_HR(textures[i].As(&resource));
			HANDLE handle = nullptr;
			BRIDGE_HR(resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &handle));
			struct ScopedHandle { HANDLE value; ~ScopedHandle() { CloseHandle(value); } } scoped_handle{handle};
			VkExternalMemoryImageCreateInfo image_external = {};
			image_external.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
			image_external.handleTypes = external.handleType;
			VkImageCreateInfo image = {};
			image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO; image.pNext = &image_external;
			image.imageType = query.type; image.format = query.format; image.extent = {width, height, 1};
			image.mipLevels = 1; image.arrayLayers = 1; image.samples = VK_SAMPLE_COUNT_1_BIT;
			image.tiling = query.tiling; image.usage = query.usage; image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			BRIDGE_VK(vkCreateImage(device, &image, nullptr, &images[i]));
			VkMemoryRequirements requirements;
			vkGetImageMemoryRequirements(device, images[i], &requirements);
			VkMemoryWin32HandlePropertiesKHR hp = {};
			hp.sType = VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR;
			BRIDGE_VK(get_handle_properties(device, external.handleType, handle, &hp));
			uint32_t types = requirements.memoryTypeBits & hp.memoryTypeBits;
			ERR_FAIL_COND_V_MSG(types == 0, ERR_UNAVAILABLE, "VULKAN_D3D11: incompatible memory types.");
			uint32_t type = 0; while (!(types & (1u << type))) { ++type; }
			VkMemoryDedicatedAllocateInfo dedicated = {};
			dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO; dedicated.image = images[i];
			VkImportMemoryWin32HandleInfoKHR import = {};
			import.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR;
			import.pNext = &dedicated; import.handleType = external.handleType; import.handle = handle;
			VkMemoryAllocateInfo allocation = {};
			allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; allocation.pNext = &import;
			allocation.allocationSize = requirements.size; allocation.memoryTypeIndex = type;
			BRIDGE_VK(vkAllocateMemory(device, &allocation, nullptr, &memories[i]));
			BRIDGE_VK(vkBindImageMemory(device, images[i], memories[i], 0));
		}
		print_line(vformat("VULKAN_D3D11_GPU active adapter=%s feature_level=0x%x width=%d height=%d buffers=%d readback=0", properties.properties.deviceName, actual, width, height, COUNT));
		return OK;
	}

	Error present(uint32_t index, bool vsync) {
		uint64_t before = OS::get_singleton()->get_ticks_usec();
		HRESULT lock = mutexes[index]->AcquireSync(1, 2000);
		ERR_FAIL_COND_V_MSG(lock != S_OK, FAILED, vformat("VULKAN_D3D11 acquire failed 0x%x", (uint32_t)lock));
		uint64_t waited = OS::get_singleton()->get_ticks_usec() - before;
		wait_usec += waited; wait_max_usec = MAX(wait_max_usec, waited);
		uint64_t acquired = before + waited;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> backbuffer;
		HRESULT buffer_result = S_OK;
		if (reference_present) { buffer_result = swapchain->GetBuffer(0, IID_PPV_ARGS(backbuffer.GetAddressOf())); }
		if (FAILED(buffer_result)) { mutexes[index]->ReleaseSync(0); return FAILED; }
		uint64_t buffered = trace_enabled ? OS::get_singleton()->get_ticks_usec() : 0;
		context->CopyResource(reference_present ? backbuffer.Get() : cached_backbuffer.Get(), textures[index].Get());
		uint64_t copied = trace_enabled ? OS::get_singleton()->get_ticks_usec() : 0;
		BRIDGE_HR(mutexes[index]->ReleaseSync(0));
		uint64_t released = trace_enabled ? OS::get_singleton()->get_ticks_usec() : 0;
		// Present submits this immediate-context copy; an extra per-frame Flush is redundant.
		if (reference_present) { context->Flush(); }
		uint64_t flushed = trace_enabled ? OS::get_singleton()->get_ticks_usec() : 0;
		BRIDGE_HR(swapchain->Present(vsync ? 1 : 0, 0));

		if (trace_enabled) {
			uint64_t done = OS::get_singleton()->get_ticks_usec();
			if (phases.size() < phases.capacity()) { phases.push_back({done, previous_present_tick ? done - previous_present_tick : 0, pending_submit_usec, waited, buffered-acquired, copied-buffered, released-copied, flushed-released, done-flushed}); }
			previous_present_tick = done;
		}
		if (++presents % 600 == 0 && trace_enabled) {
			print_line(vformat("VULKAN_D3D11_GPU auxiliary presents=%d keyed_wait_mean_us=%d keyed_wait_max_us=%d readback=0", presents, wait_usec / presents, wait_max_usec));
		}
		return OK;
	}
#undef BRIDGE_HR
#undef BRIDGE_VK
};
