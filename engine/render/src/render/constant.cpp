// Copyright 2020 The Defold Foundation
// Licensed under the Defold License version 1.0 (the "License"); you may not use
// this file except in compliance with the License.
//
// You may obtain a copy of the License, together with FAQs at
// https://www.defold.com/license
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

#include <dlib/memory.h>
#include <render/render.h>
#include <render/render_private.h>

namespace dmRender
{

/////////////////////////////////////////////////////////////////////////////////////////////////////////

Constant::Constant() {}
Constant::Constant(dmhash_t name_hash, int32_t location)
    : m_Values(0)
    , m_NameHash(name_hash)
    , m_Type(dmRenderDDF::MaterialDesc::CONSTANT_TYPE_USER)
    , m_Location(location)
    , m_NumValues(0)
{
}

HConstant NewConstant(dmhash_t name_hash)
{
    return new Constant(name_hash, -1);
}

void DeleteConstant(HConstant constant)
{
    dmMemory::AlignedFree(constant->m_Values);
    delete constant;
}

dmVMath::Vector4* GetConstantValues(HConstant constant, uint32_t* num_values)
{
    *num_values = constant->m_NumValues;
    return constant->m_Values;
}

Result SetConstantValues(HConstant constant, dmVMath::Vector4* values, uint32_t num_values)
{
    if (num_values > constant->m_NumValues)
    {
        dmVMath::Vector4* newmem = 0;
        if (dmMemory::RESULT_OK != dmMemory::AlignedMalloc((void**)&newmem, 16,  num_values * sizeof(dmVMath::Vector4)))
        {
            return RESULT_OUT_OF_RESOURCES;
        }
        dmMemory::AlignedFree(constant->m_Values);
        constant->m_Values = newmem;
    }

    memcpy(constant->m_Values, values, num_values * sizeof(dmVMath::Vector4));
    constant->m_NumValues = num_values;
    return dmRender::RESULT_OK;
}

dmhash_t GetConstantName(HConstant constant)
{
    return constant->m_NameHash;
}

void SetConstantName(HConstant constant, dmhash_t name)
{
    constant->m_NameHash = name;
}

int32_t GetConstantLocation(HConstant constant)
{
    return constant->m_Location;
}

void SetConstantLocation(HConstant constant, int32_t location)
{
    constant->m_Location = location;
}

dmRenderDDF::MaterialDesc::ConstantType GetConstantType(HConstant constant)
{
    return constant->m_Type;
}

void SetConstantType(HConstant constant, dmRenderDDF::MaterialDesc::ConstantType type)
{
    constant->m_Type = type;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////

struct NamedConstantBuffer
{
    struct Constant
    {
        dmhash_t   m_NameHash;
        uint32_t   m_ValueIndex;
        uint32_t   m_NumValues;
    };

    dmHashTable64<Constant>     m_Constants;
    dmArray<dmVMath::Vector4>   m_Values;
};

HNamedConstantBuffer NewNamedConstantBuffer()
{
    HNamedConstantBuffer buffer = new NamedConstantBuffer();
    buffer->m_Constants.SetCapacity(9, 16);
    return buffer;
}

void DeleteNamedConstantBuffer(HNamedConstantBuffer buffer)
{
    delete buffer;
}

void ClearNamedConstantBuffer(HNamedConstantBuffer buffer)
{
    buffer->m_Constants.Clear();
    buffer->m_Values.SetSize(0);
}

struct ShiftConstantsContext
{
    uint32_t m_Index;
    uint32_t m_NumValues;
};

static inline void ShiftConstantIndices(ShiftConstantsContext* context, const uint64_t* name_hash, NamedConstantBuffer::Constant* constant)
{
    if (constant->m_ValueIndex > context->m_Index)
    {
        constant->m_ValueIndex -= context->m_NumValues;
    }
}

void RemoveNamedConstant(HNamedConstantBuffer buffer, dmhash_t name_hash)
{
    NamedConstantBuffer::Constant* c = buffer->m_Constants.Get(name_hash);
    if (!c)
        return;

    uint32_t values_index = c->m_ValueIndex;
    uint32_t num_values = c->m_NumValues;

    dmVMath::Vector4* p_current = &buffer->m_Values[values_index];
    // shift the data "left" by num_values
    uint32_t remaining = buffer->m_Values.Size() - (values_index + num_values);

    dmVMath::Vector4* p_next = p_current + num_values;
    memmove(p_current, p_next, remaining); // if it's the last item, then "remaining" will be 0

    buffer->m_Constants.Erase(name_hash);
    buffer->m_Values.SetSize(buffer->m_Values.Size() - num_values);

    ShiftConstantsContext shift_context;
    shift_context.m_Index = values_index;
    shift_context.m_NumValues = num_values;
    buffer->m_Constants.Iterate(ShiftConstantIndices, &shift_context);
}

void SetNamedConstant(HNamedConstantBuffer buffer, dmhash_t name_hash, dmVMath::Vector4* values, uint32_t num_values)
{
    dmHashTable64<NamedConstantBuffer::Constant>& constants = buffer->m_Constants;

    NamedConstantBuffer::Constant* c = constants.Get(name_hash);
    if (c && c->m_NumValues != num_values)
    {
        RemoveNamedConstant(buffer, name_hash);
        c = 0;
    }

    if (c == 0)
    {
        if (constants.Full())
        {
            uint32_t capacity = constants.Capacity() + 8;
            constants.SetCapacity(capacity, capacity * 2);
        }

        if (buffer->m_Values.Remaining() < num_values)
            buffer->m_Values.OffsetCapacity(num_values - buffer->m_Values.Remaining());

        uint32_t values_index = buffer->m_Values.Size();

        buffer->m_Values.SetSize(buffer->m_Values.Size() + num_values);

        NamedConstantBuffer::Constant constant;
        constant.m_NameHash    = name_hash;
        constant.m_NumValues   = num_values;
        constant.m_ValueIndex  = values_index;
        constants.Put(name_hash, constant);

        // Get the pointer
        c = constants.Get(name_hash);
    }

    dmVMath::Vector4* p = &buffer->m_Values[c->m_ValueIndex];
    memcpy(p, values, sizeof(values[0]) * num_values);
}

void SetNamedConstants(HNamedConstantBuffer buffer, HConstant* constants, uint32_t num_constants)
{
    for (uint32_t i = 0; i < num_constants; ++i)
    {
        Constant* c = constants[i];
        SetNamedConstant(buffer, c->m_NameHash, c->m_Values, c->m_NumValues);
    }
}

bool GetNamedConstant(HNamedConstantBuffer buffer, dmhash_t name_hash, dmVMath::Vector4** values, uint32_t* num_values)
{
    NamedConstantBuffer::Constant* c = buffer->m_Constants.Get(name_hash);
    if (!c)
        return false;

    *values = &buffer->m_Values[c->m_ValueIndex];
    *num_values = c->m_NumValues;
    return true;
}

struct ApplyConstantContext
{
    dmGraphics::HContext m_GraphicsContext;
    HMaterial            m_Material;
    HNamedConstantBuffer m_ConstantBuffer;
    ApplyConstantContext(dmGraphics::HContext graphics_context, HMaterial material, HNamedConstantBuffer constant_buffer)
    {
        m_GraphicsContext = graphics_context;
        m_Material = material;
        m_ConstantBuffer = constant_buffer;
    }
};

static inline void ApplyConstant(ApplyConstantContext* context, const uint64_t* name_hash, NamedConstantBuffer::Constant* constant)
{
    int32_t* location = context->m_Material->m_NameHashToLocation.Get(*name_hash);
    if (location)
    {
        dmVMath::Vector4* values = &context->m_ConstantBuffer->m_Values[constant->m_ValueIndex];
        dmGraphics::SetConstantV4(context->m_GraphicsContext, values, constant->m_NumValues, *location);
    }
}

void ApplyNamedConstantBuffer(dmRender::HRenderContext render_context, HMaterial material, HNamedConstantBuffer buffer)
{
    dmGraphics::HContext graphics_context = dmRender::GetGraphicsContext(render_context);
    ApplyConstantContext context(graphics_context, material, buffer);
    buffer->m_Constants.Iterate(ApplyConstant, &context);
}

}