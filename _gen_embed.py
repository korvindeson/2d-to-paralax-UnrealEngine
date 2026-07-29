import base64, os

sources = [
    "DepthDebugVisualizerComponent.cpp",
    "DepthDebugVisualizerComponent.h",
    "FaceParallaxComponent.cpp",
    "FaceParallaxComponent.h",
    "FaceParallaxEditorWidget.cpp",
    "FaceParallaxEditorWidget.h",
    "FaceParallaxPreset.cpp",
    "FaceParallaxPreset.h",
    "FaceParallaxPreviewActor.cpp",
    "FaceParallaxPreviewActor.h",
    "FaceParallaxTypes.h",
]

this_dir = os.path.dirname(os.path.abspath(__file__))
for name in sources:
    path = os.path.join(this_dir, name)
    with open(path, "rb") as f:
        data = base64.b64encode(f.read()).decode("ascii")
    print(f'    "{name}": base64.b64decode({repr(data)}),')
