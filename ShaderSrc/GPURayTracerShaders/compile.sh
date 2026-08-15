./../../Slang/bin/slangc RayTracing.slang -target spirv -o RayTracing.spv
./../../Slang/bin/slangc RayTracing.slang -DSER -target spirv -o RayTracing_SER.spv
./../../Slang/bin/slangc DisplayImage.slang -target spirv -o DisplayImage.spv