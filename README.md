# TomCat Engine — dev_opengl3D

**Languages**: English | [简体中文](README.zh-CN.md)

This page covers this branch's changes and progress. See the [main branch README](https://github.com/chnnasn/TomCat_Engine/blob/main/README.md) for the project overview, shared features, showcase and desktop build baseline; branch-specific requirements and entry points are below.

Documentation updated **2026-09-21**. Validation below cites existing records; this documentation-only update did not rerun engine tests.

## Branch Purpose

Extends the 2D baseline with static model import, six built-in primitives, sky/HDR environments, editable lights, directional shadows and PBR. Meshes and components integrate with assets, Prefabs, Cook and Player. Start with the [3D guide](docs/OPENGL_3D.zh-CN.md), [primitive sample](Samples/Primitives3D/README.md) and [lighting sample](Samples/PBRLighting/README.md). The documented validation target is Windows OpenGL 4.6; 3D physics and skeletal animation are not implemented.

## Current Progress

- [x] Static OBJ/FBX/glTF/GLB model import and six built-in primitives: Cube, Sphere, Capsule, Cylinder, Plane and Quad.
- [x] Sky/HDR panoramas, directional/point/spot lights, directional shadows and metallic/roughness PBR with environment lighting.
- [x] Mesh, Light3D and Environment3D integrate with component registration, C# access, asset handles, scene/Prefab serialization, Cook and Player.

## Getting Started

- [3D guide and build requirements](docs/OPENGL_3D.zh-CN.md): Windows OpenGL 4.6, VS 2022 C++, CMake, .NET 10 and initialized Assimp submodule.
- Open the [primitives](Samples/Primitives3D/README.md) or [PBR lighting](Samples/PBRLighting/README.md) sample. Disable Scene 2D mode and use a perspective camera.

## Validation Status

The 3D guide records Windows OpenGL 4.6 acceptance. `Renderer3DRegression` covers serialization, Prefabs, Cook references and model artifacts; `--gpu` adds picking, camera-buffer switching, lighting/shadow/PBR/HDR and GL-state checks. Use the commands in that guide for reproduction.

## Limits and Remaining Work

- No 3D rigid bodies or skeletal animation. Shadows cover one directional light only.
- Standalone material assets, PBR texture maps, automatic model PBR import, transparent material sorting and additional shadow types remain incomplete. Web shader adaptation is not browser acceptance.
