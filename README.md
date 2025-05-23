# Houdini PCG Translator

Welcome to the [repository](https://github.com/AdrianPanGithub/HoudiniPCGTranslator) for the Houdini PCG Translator For Unreal.

This plug-in provides seamless integration between Houdini Engine and Unreal PCG, without any exchange files. Allow both PCGDataAsset input and output, and PCGComponent input.
Also see [Tutorial](https://youtu.be/MLbhgsCSqoQ)

# Compatibility

This plug-in relies on **my custom** [HoudiniEngineForUnreal](https://github.com/AdrianPanGithub/HoudiniEngineForUnreal), so similar [compatibility](https://github.com/AdrianPanGithub/HoudiniEngineForUnreal#compatibility) as the Houdini Engine, but only support Unreal >= 5.4 (Examples only run with 5.5).

# Installation
01. In this GitHub [repository](https://github.com/AdrianPanGithub/HoudiniPCGTranslator), click [Releases](https://github.com/AdrianPanGithub/HoudiniPCGTranslator/releases) on the right side. 
02. Download the Houdini PCG Translator zip file that matches your Unreal Engine Version.
03. Extract the **HoudiniPCGTranslator** and **HoudiniEngine** to the **Plugins** of your Unreal Project Directory.

    e.g. `C:/Unreal Projects/MyGameProject/Plugins/HoudiniPCGTranslator` and `C:/Unreal Projects/MyGameProject/Plugins/HoudiniEngine`

# Tutorial/Example
[Tutorial](https://youtu.be/MLbhgsCSqoQ)
Require Unreal Engine >= 5.5 to run these examples.
See `/HoudiniPCGTranslator/Example/EUBP_HE_Example_PCGDataInput` and `/HoudiniPCGTranslator/Example/he_example_pcg_data_output` in the content of this plug-in.

# Usage Brief

Support both input and output of PCGDataAsset and input of PCGComponent(**Wire the data to the output node in PCGGraph**, then Using actors(world) input)

Support PCGPointData, PCGSplineData and PCGDynamicMeshData(>= 5.5)

Here are some attributes for PCG data input and output

i@**unreal_output_pcg_data_asset**

    = 1 on detail, curves/points will output as PCGSplineData/PCGPointData/PCGDynamicMeshData in PCGDataAsset
@**unreal_pcg_attribute_***

    define a PCG attribute, support float, int, vectors, transform, quaternion, object path, string, etc.
s[]@**unreal_pcg_tags**

    Tags on PCGData, useful when output splines that tagged with building grammers. Could be either string or string array.
s@**unreal_object_path**

    define where PCGDataAsset should be created at.
@**P, p@rot, v@scale**

    General attributes for PCGSplineData and PCGPointData input and output. For PCGSplineData input, must add parm tag { import_rot_and_scale = 1 } to operator path input parm.
f@**density, v@Cd, f@Alpha**

    General attributes for both PCGPointData input and output
v@**unreal_spline_point_arrive_tangent**, v@**unreal_spline_point_leave_tangent**

    Attributes for specify point tangents on PCGSplineData
i@**curve_closed**

    on curves, to define whether a spline is closed or open
i@**curve_type**

    if no tangents found, then this attribute can be used to set spline point as linear( = 0)