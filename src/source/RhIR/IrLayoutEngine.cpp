#include "RhIR/IrLayoutEngine.h"

#include "RhIR/IrModule.h"
#include "RhIR/IrType.h"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace rhc {

namespace {

struct RawLayout {
    std::uint64_t size = 0;
    std::uint64_t align = 1;
};

std::uint64_t alignUp(std::uint64_t value, std::uint64_t align) {
    return align <= 1 ? value : (value + align - 1) / align * align;
}

bool validTypeId(TypeId id, std::size_t typeCount) {
    return id != InvalidType && static_cast<std::size_t>(id) < typeCount;
}

void addError(std::vector<IrLayoutError>& errors,
              IrLayoutErrorCode code,
              TypeId type,
              std::string message) {
    errors.push_back(IrLayoutError{code, type, std::move(message)});
}

bool fitsU32(std::uint64_t value) {
    return value <= std::numeric_limits<std::uint32_t>::max();
}

RawLayout layoutType(TypeId id,
                     std::vector<IrType>& types,
                     std::unordered_map<TypeId, RawLayout>& memo,
                     std::vector<TypeId>& visiting,
                     std::vector<IrLayoutError>& errors,
                     const IrTargetProvider& provider) {
    if (!validTypeId(id, types.size())) {
        addError(errors, IrLayoutErrorCode::InvalidTypeId, id, "TypeId invalid");
        return {};
    }

    if (const auto it = memo.find(id); it != memo.end()) {
        return it->second;
    }

    if (std::find(visiting.begin(), visiting.end(), id) != visiting.end()) {
        addError(errors, IrLayoutErrorCode::RecursiveByValue, id,
                 "tipe rekursif by-value tanpa batas pointer");
        return {};
    }

    visiting.push_back(id);
    const IrTypeKind kind = types[id].kind;
    RawLayout layout{};
    bool ok = true;

    switch (kind) {
    case IrTypeKind::Void:
        layout = {0, 1};
        break;

    case IrTypeKind::Bool:
        layout = {1, 1};
        break;

    case IrTypeKind::Int: {
        const std::uint64_t size = types[id].sizeBytes;
        if (size != 1 && size != 2 && size != 4 && size != 8) {
            addError(errors, IrLayoutErrorCode::InvalidIntegerSize, id,
                     "integer sizeBytes harus 1, 2, 4, atau 8 (typeId=" +
                     std::to_string(id) + " sizeBytes=" +
                     std::to_string(types[id].sizeBytes) + ")");
            ok = false;
            break;
        }
        const std::uint64_t align =
            std::min<std::uint64_t>(size, provider.dataLayout().maxNaturalAlignmentBytes);
        layout = {size, align};
        break;
    }

    case IrTypeKind::Float: {
        // desimal = double 64-bit; float 32-bit TIDAK ADA di bahasa.
        const std::uint64_t size = types[id].sizeBytes;
        if (size != 8) {
            addError(errors, IrLayoutErrorCode::InvalidFloatSize, id,
                     "float sizeBytes harus 8 (desimal = double 64-bit)");
            ok = false;
            break;
        }
        const std::uint64_t align =
            std::min<std::uint64_t>(size, provider.dataLayout().maxNaturalAlignmentBytes);
        layout = {size, align};
        break;
    }

    case IrTypeKind::Pointer: {
        if (!validTypeId(types[id].elementType, types.size())) {
            addError(errors, IrLayoutErrorCode::InvalidPointerElement, id,
                     "pointer elementType invalid");
            ok = false;
            break;
        }
        layout = {provider.dataLayout().pointerSizeBytes,
                  provider.dataLayout().pointerAlignmentBytes};
        break;
    }

    case IrTypeKind::Array: {
        if (!validTypeId(types[id].elementType, types.size())) {
            addError(errors, IrLayoutErrorCode::InvalidArrayElement, id,
                     "array elementType invalid");
            ok = false;
            break;
        }
        const RawLayout element = layoutType(types[id].elementType, types, memo, visiting,
                                             errors, provider);
        if (element.align == 0 && errors.empty()) {
            addError(errors, IrLayoutErrorCode::InvalidArrayElement, id,
                     "array element tidak punya layout valid");
            ok = false;
            break;
        }
        const std::uint64_t count = types[id].arraySize;
        if (count > 0 && element.size == 0) {
            addError(errors, IrLayoutErrorCode::InvalidArrayElement, id,
                     "array berisi elemen berukuran nol");
            ok = false;
            break;
        }
        const std::uint64_t size =
            count == 0 ? 0 : alignUp(element.size, element.align) * count;
        layout = {size, element.align};
        break;
    }

    case IrTypeKind::View: {
        if (!validTypeId(types[id].elementType, types.size())) {
            addError(errors, IrLayoutErrorCode::InvalidViewElement, id,
                     "view elementType invalid");
            ok = false;
            break;
        }
        layout = {static_cast<std::uint64_t>(provider.dataLayout().pointerSizeBytes) * 2,
                  provider.dataLayout().pointerAlignmentBytes};
        break;
    }

    case IrTypeKind::Struct: {
        std::uint64_t offset = 0;
        std::uint64_t maxAlign = 1;
        for (IrField& field : types[id].fields) {
            if (field.bitWidth != 0) {
                addError(errors, IrLayoutErrorCode::UnsupportedBitfield, id,
                         "bitfield belum didukung layout engine");
                ok = false;
                continue;
            }
            if (!validTypeId(field.type, types.size())) {
                addError(errors, IrLayoutErrorCode::InvalidTypeId, id,
                         "field struct punya TypeId invalid");
                ok = false;
                continue;
            }
            const RawLayout fieldLayout = layoutType(field.type, types, memo, visiting,
                                                     errors, provider);
            if (fieldLayout.align == 0) {
                ok = false;
                continue;
            }
            offset = alignUp(offset, fieldLayout.align);
            field.offsetBytes = static_cast<std::uint32_t>(offset);
            offset += fieldLayout.size;
            maxAlign = std::max(maxAlign, fieldLayout.align);
        }
        layout = {alignUp(offset, maxAlign), maxAlign};
        break;
    }

    case IrTypeKind::Sum: {
        bool plainEnum = true;
        std::uint64_t payloadSize = 0;
        std::uint64_t payloadAlign = 1;
        for (const IrVariant& variant : types[id].variants) {
            if (variant.payloadType == InvalidType) {
                continue;
            }
            plainEnum = false;
            if (!validTypeId(variant.payloadType, types.size())) {
                addError(errors, IrLayoutErrorCode::InvalidTypeId, id,
                         "payload sum punya TypeId invalid");
                ok = false;
                continue;
            }
            const RawLayout payload = layoutType(variant.payloadType, types, memo, visiting,
                                                 errors, provider);
            if (payload.align == 0) {
                ok = false;
                continue;
            }
            payloadSize = std::max(payloadSize, payload.size);
            payloadAlign = std::max(payloadAlign, payload.align);
        }

        if (plainEnum) {
            layout = {8, 8};
        } else {
            payloadAlign = std::max<std::uint64_t>(payloadAlign, 8);
            const std::uint64_t tagOffset = alignUp(0, payloadAlign);
            const std::uint64_t payloadOffset = tagOffset + 8;
            layout = {alignUp(payloadOffset + alignUp(payloadSize, payloadAlign), payloadAlign),
                      payloadAlign};
        }
        break;
    }

    case IrTypeKind::FnPtr: {
        if (types[id].returnType == InvalidType) {
            addError(errors, IrLayoutErrorCode::InvalidFunctionSignature, id,
                     "fnptr returnType invalid");
            ok = false;
        }
        for (const TypeId param : types[id].paramTypes) {
            if (!validTypeId(param, types.size())) {
                addError(errors, IrLayoutErrorCode::InvalidFunctionSignature, id,
                         "fnptr paramType invalid");
                ok = false;
            }
        }
        layout = {provider.dataLayout().pointerSizeBytes,
                  provider.dataLayout().pointerAlignmentBytes};
        break;
    }
    }

    visiting.pop_back();

    if (!ok || layout.align == 0) {
        return {};
    }
    if (!fitsU32(layout.size) || !fitsU32(layout.align)) {
        addError(errors, IrLayoutErrorCode::LayoutTooLarge, id,
                 "layout melebihi representasi 32-bit IrType");
        return {};
    }

    types[id].sizeBytes = static_cast<std::uint32_t>(layout.size);
    types[id].alignBytes = static_cast<std::uint32_t>(layout.align);
    memo.emplace(id, layout);
    return layout;
}

}  // namespace

IrLayoutReport IrLayoutEngine::layoutModule(IrModule& module) const {
    IrLayoutReport report;
    std::vector<IrType> stagedTypes = module.types;
    std::unordered_map<TypeId, RawLayout> memo;
    std::vector<TypeId> visiting;

    for (std::size_t index = 0; index < stagedTypes.size(); ++index) {
        layoutType(static_cast<TypeId>(index), stagedTypes, memo, visiting, report.errors,
                   provider_);
    }

    if (report.ok()) {
        module.types = std::move(stagedTypes);
    }
    return report;
}

}  // namespace rhc
