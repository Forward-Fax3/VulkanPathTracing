$common = @("-profile", "spirv_1_6", "-O3", "-target", "spirv",
"-I", "../GeneralHelperShaders",
"-I", "../../OOPWithCpp/3rdParty/Projects/git/NRD/Shaders")

$slangc = "./../../Slang/bin/slangc.exe"

& $slangc RayTracing.slang @common -capability spvRayQueryKHR+spvRayTracingPositionFetchKHR -o RayTracing.spv
& $slangc RayTracing.slang @common -capability spvRayQueryKHR+spvRayTracingPositionFetchKHR+spvShaderInvocationReorderEXT -DSER -o RayTracing_SER.spv
& $slangc DisplayImage.slang @common -o DisplayImage.spv