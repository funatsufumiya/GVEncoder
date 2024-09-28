# GVEncoder

- oF 0.11 - 0.12
- [GV Video](https://github.com/Ushio/ofxExtremeGpuVideo) Encoder using `ofxGVTextureSerializer`.
- seems faster than original `nvtt` encoder.
- `DXT5` is only supported.

## Usage

- drop int window, a dir contains images to encode. (or add with gui)
- press `encode` button.
- you can set cores numbers to encode, and video fps.

## Dependencies

- [ofxGVTextureSerializer](https://github.com/funatsufumiya/ofxGVTextureSerializer)
- [ofxAsync](https://github.com/funatsufumiya/ofxAsync)
- [ofxImGui](https://github.com/jvcleave/ofxImGui) (note: use develop branch!)