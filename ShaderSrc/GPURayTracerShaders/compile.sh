./../../Slang/bin/slangc RayTracing.slang spirv_1_6 -capability spvRayQueryKHR+spvShaderInvocationReorderEXT -O3 -target spirv -o RayTracing.spv
./../../Slang/bin/slangc RayTracing.slang spirv_1_6 -capability spvRayQueryKHR+spvShaderInvocationReorderEXT -O3 -target spirv -o RayTracing_SER.spv
./../../Slang/bin/slangc DisplayImage.slang spirv_1_6 -O3 -target spirv -o DisplayImage.spv