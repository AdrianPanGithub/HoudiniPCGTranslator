# Houdini PCG Translator

Welcome to the [repository](https://github.com/AdrianPanGithub/HoudiniPCGTranslator) for the Houdini PCG Translator For Unreal.

This plug-in provides seamless integration between Houdini Engine and Unreal PCG, without any exchange files. Allow both PCGDataAsset input and output, and PCGComponent input.
Also see [Tutorial](https://www.youtube.com/@adrianpan9211)

# Compatibility

This plug-in is reply on **my custom** [HoudiniEngineForUnreal](https://github.com/AdrianPanGithub/HoudiniEngineForUnreal), so similar compatibility as the Houdini Engine, but only support Unreal >= 5.4 (Examples only run with 5.5).

# Installation
01. In this GitHub [repository](https://github.com/AdrianPanGithub/HoudiniPCGTranslator), click **Releases** on the right side. 
02. Download the Houdini PCG Transaltor zip file that matches your Unreal Engine Version.
03. Extract the **HoudiniPCGTranslator** and **HoudiniEngine** to the **Plugins** of your Unreal Project Directory.

    e.g. `C:/Unreal Projects/MyGameProject/Plugins/HoudiniPCGTranslator` and `C:/Unreal Projects/MyGameProject/Plugins/HoudiniEngine`

# Tutorial/Example
[Tutorial](https://www.youtube.com/@adrianpan9211)
Require Unreal Engine >= 5.5 to run these examples.
See `/HoudiniPCGTranslator/Example/EUBP_HE_Example_PCGDataInput` and `/HoudiniPCGTranslator/Example/he_example_pcg_data_output` in the content of this plug-in.

# Usage Brief

Support both input and output of PCGDataAsset and input of PCGComponent(Using actors(world) input)

Support PCGPointData, PCGSplineData and PCGDynamicMeshData(>= 5.5)

Here are some attributes for PCG data input and output

i@**unreal_output_pcg_data_asset**

    = 1 on detail, curves/points will output as PCGSplineData/PCGPointData in PCGDataAsset
@**unreal_pcg_attribute_***

    define a PCG attribute, support float, int, vectors, transform, quaternion, object path, string etc.
@**P, p@rot, v@scale**

    General attributes for PCGSplineData and PCGPointData input and output
f@**density, v@Cd, f@Alpha**

    General attributes for both PCGPointData input and output
v@**unreal_spline_point_arrive_tangent**, v@**unreal_spline_point_leave_tangent**

    Attributes for specify point tangents on PCGSplineData
i@**curve_closed**

    on curves, to define whether a spline is closed or open
i@**curve_type**

    if no tangents found, then this attribute can be used to set spline point as linear( = 0)