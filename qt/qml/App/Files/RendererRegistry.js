.pragma library

var fallbackRendererId = "plain-text"

var renderers = {
    "plain-text": renderer("plain-text", "Source", "PlainTextRenderer", true, ["source"]),
    "markdown": renderer("markdown", "Markdown", "MarkdownRenderer", true, ["rendered", "source", "split"]),
    "image": renderer("image", "Image", "ImageRenderer", false, ["rendered"]),
    "svg": renderer("svg", "SVG", "SvgRenderer", false, ["rendered", "source", "split"]),
    "structured-json": renderer("structured-json", "JSON", "StructuredDataRenderer", false, ["structured", "source", "split"]),
    "structured-yaml": renderer("structured-yaml", "YAML", "StructuredDataRenderer", false, ["structured", "source", "split"]),
    "pdf": renderer("pdf", "PDF", "PdfRenderer", false, ["rendered"]),
    "model-3d": renderer("model-3d", "3D Model", "ModelRenderer", false, ["rendered"]),
    "audio": renderer("audio", "Audio", "AudioRenderer", false, ["rendered"]),
    "video": renderer("video", "Video", "VideoRenderer", false, ["rendered"]),
    "binary": renderer("binary", "Binary Metadata", "BinaryMetadataRenderer", false, ["metadata"])
}

function renderer(id, displayLabel, componentName, available, modes) {
    return {
        id: id,
        displayLabel: displayLabel,
        componentName: componentName,
        available: available,
        modes: modes
    }
}

function hasMode(modes, mode) {
    return modes.indexOf(mode) >= 0
}

function copyRenderer(definition, requestedRendererId, usedFallback) {
    return {
        id: definition.id,
        displayLabel: definition.displayLabel,
        componentName: definition.componentName,
        available: definition.available,
        modes: definition.modes.slice(),
        supportsSource: hasMode(definition.modes, "source"),
        supportsRendered: hasMode(definition.modes, "rendered"),
        supportsStructured: hasMode(definition.modes, "structured"),
        supportsSplit: hasMode(definition.modes, "split"),
        requestedRendererId: requestedRendererId,
        usedFallback: usedFallback
    }
}

function definitionFor(rendererId) {
    return renderers[String(rendererId || "")] || null
}

function resolve(fileIdentity, explicitRendererId) {
    var identity = fileIdentity || ({})
    var requestedRendererId = String(
        explicitRendererId
            || identity.preferredRendererId
            || fallbackRendererId
    )

    var requested = definitionFor(requestedRendererId)

    if (requested && requested.available) {
        return copyRenderer(requested, requestedRendererId, false)
    }

    return copyRenderer(
        renderers[fallbackRendererId],
        requestedRendererId,
        requestedRendererId !== fallbackRendererId
    )
}
