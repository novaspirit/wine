/*
 * Copyright 2009 Henri Verbeet for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 */

#include "config.h"
#include "wine/port.h"

#include "d3d11_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d11);

static HRESULT isgn_handler(const char *data, DWORD data_size, DWORD tag, void *ctx)
{
    struct wined3d_shader_signature *is = ctx;

    switch(tag)
    {
        case TAG_ISGN:
            return shader_parse_signature(data, data_size, is);

        default:
            FIXME("Unhandled chunk %s.\n", debugstr_an((const char *)&tag, 4));
            return S_OK;
    }
}

static HRESULT d3d10_input_layout_to_wined3d_declaration(const D3D10_INPUT_ELEMENT_DESC *element_descs,
        UINT element_count, const void *shader_byte_code, SIZE_T shader_byte_code_length,
        struct wined3d_vertex_element **wined3d_elements)
{
    struct wined3d_shader_signature is;
    HRESULT hr;
    UINT i;

    hr = parse_dxbc(shader_byte_code, shader_byte_code_length, isgn_handler, &is);
    if (FAILED(hr))
    {
        ERR("Failed to parse input signature.\n");
        return E_FAIL;
    }

    *wined3d_elements = HeapAlloc(GetProcessHeap(), 0, element_count * sizeof(**wined3d_elements));
    if (!*wined3d_elements)
    {
        ERR("Failed to allocate wined3d vertex element array memory.\n");
        HeapFree(GetProcessHeap(), 0, is.elements);
        return E_OUTOFMEMORY;
    }

    for (i = 0; i < element_count; ++i)
    {
        struct wined3d_vertex_element *e = &(*wined3d_elements)[i];
        const D3D10_INPUT_ELEMENT_DESC *f = &element_descs[i];
        UINT j;

        e->format = wined3dformat_from_dxgi_format(f->Format);
        e->input_slot = f->InputSlot;
        e->offset = f->AlignedByteOffset;
        e->output_slot = WINED3D_OUTPUT_SLOT_UNUSED;
        e->input_slot_class = f->InputSlotClass;
        e->instance_data_step_rate = f->InstanceDataStepRate;
        e->method = WINED3D_DECL_METHOD_DEFAULT;
        e->usage = 0;
        e->usage_idx = 0;

        for (j = 0; j < is.element_count; ++j)
        {
            if (!strcmp(element_descs[i].SemanticName, is.elements[j].semantic_name)
                    && element_descs[i].SemanticIndex == is.elements[j].semantic_idx)
            {
                e->output_slot = is.elements[j].register_idx;
                break;
            }
        }
    }

    shader_free_signature(&is);

    return S_OK;
}

static inline struct d3d10_input_layout *impl_from_ID3D10InputLayout(ID3D10InputLayout *iface)
{
    return CONTAINING_RECORD(iface, struct d3d10_input_layout, ID3D10InputLayout_iface);
}

/* IUnknown methods */

static HRESULT STDMETHODCALLTYPE d3d10_input_layout_QueryInterface(ID3D10InputLayout *iface,
        REFIID riid, void **object)
{
    TRACE("iface %p, riid %s, object %p\n", iface, debugstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_ID3D10InputLayout)
            || IsEqualGUID(riid, &IID_ID3D10DeviceChild)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        IUnknown_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d10_input_layout_AddRef(ID3D10InputLayout *iface)
{
    struct d3d10_input_layout *This = impl_from_ID3D10InputLayout(iface);
    ULONG refcount = InterlockedIncrement(&This->refcount);

    TRACE("%p increasing refcount to %u\n", This, refcount);

    if (refcount == 1)
    {
        wined3d_mutex_lock();
        wined3d_vertex_declaration_incref(This->wined3d_decl);
        wined3d_mutex_unlock();
    }

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d10_input_layout_Release(ID3D10InputLayout *iface)
{
    struct d3d10_input_layout *This = impl_from_ID3D10InputLayout(iface);
    ULONG refcount = InterlockedDecrement(&This->refcount);

    TRACE("%p decreasing refcount to %u\n", This, refcount);

    if (!refcount)
    {
        wined3d_mutex_lock();
        wined3d_vertex_declaration_decref(This->wined3d_decl);
        wined3d_mutex_unlock();
    }

    return refcount;
}

/* ID3D10DeviceChild methods */

static void STDMETHODCALLTYPE d3d10_input_layout_GetDevice(ID3D10InputLayout *iface, ID3D10Device **device)
{
    FIXME("iface %p, device %p stub!\n", iface, device);
}

static HRESULT STDMETHODCALLTYPE d3d10_input_layout_GetPrivateData(ID3D10InputLayout *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d10_input_layout *layout = impl_from_ID3D10InputLayout(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n",
            iface, debugstr_guid(guid), data_size, data);

    return d3d10_get_private_data(&layout->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d10_input_layout_SetPrivateData(ID3D10InputLayout *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d10_input_layout *layout = impl_from_ID3D10InputLayout(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n",
            iface, debugstr_guid(guid), data_size, data);

    return d3d10_set_private_data(&layout->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d10_input_layout_SetPrivateDataInterface(ID3D10InputLayout *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d10_input_layout *layout = impl_from_ID3D10InputLayout(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return d3d10_set_private_data_interface(&layout->private_store, guid, data);
}

static const struct ID3D10InputLayoutVtbl d3d10_input_layout_vtbl =
{
    /* IUnknown methods */
    d3d10_input_layout_QueryInterface,
    d3d10_input_layout_AddRef,
    d3d10_input_layout_Release,
    /* ID3D10DeviceChild methods */
    d3d10_input_layout_GetDevice,
    d3d10_input_layout_GetPrivateData,
    d3d10_input_layout_SetPrivateData,
    d3d10_input_layout_SetPrivateDataInterface,
};

static void STDMETHODCALLTYPE d3d10_input_layout_wined3d_object_destroyed(void *parent)
{
    struct d3d10_input_layout *layout = parent;

    wined3d_private_store_cleanup(&layout->private_store);
    HeapFree(GetProcessHeap(), 0, parent);
}

static const struct wined3d_parent_ops d3d10_input_layout_wined3d_parent_ops =
{
    d3d10_input_layout_wined3d_object_destroyed,
};

HRESULT d3d10_input_layout_init(struct d3d10_input_layout *layout, struct d3d_device *device,
        const D3D10_INPUT_ELEMENT_DESC *element_descs, UINT element_count,
        const void *shader_byte_code, SIZE_T shader_byte_code_length)
{
    struct wined3d_vertex_element *wined3d_elements;
    HRESULT hr;

    layout->ID3D10InputLayout_iface.lpVtbl = &d3d10_input_layout_vtbl;
    layout->refcount = 1;
    wined3d_mutex_lock();
    wined3d_private_store_init(&layout->private_store);

    if (FAILED(hr = d3d10_input_layout_to_wined3d_declaration(element_descs, element_count,
            shader_byte_code, shader_byte_code_length, &wined3d_elements)))
    {
        WARN("Failed to create wined3d vertex declaration elements, hr %#x.\n", hr);
        wined3d_private_store_cleanup(&layout->private_store);
        wined3d_mutex_unlock();
        return hr;
    }

    hr = wined3d_vertex_declaration_create(device->wined3d_device, wined3d_elements, element_count,
            layout, &d3d10_input_layout_wined3d_parent_ops, &layout->wined3d_decl);
    HeapFree(GetProcessHeap(), 0, wined3d_elements);
    if (FAILED(hr))
    {
        WARN("Failed to create wined3d vertex declaration, hr %#x.\n", hr);
        wined3d_private_store_cleanup(&layout->private_store);
        wined3d_mutex_unlock();
        return hr;
    }
    wined3d_mutex_unlock();

    return S_OK;
}

struct d3d10_input_layout *unsafe_impl_from_ID3D10InputLayout(ID3D10InputLayout *iface)
{
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d10_input_layout_vtbl);

    return impl_from_ID3D10InputLayout(iface);
}