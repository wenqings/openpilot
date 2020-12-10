#include <assert.h>
#include <string.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <memory>
#include <vector>
#include "common/util.h"
#include "utilpp.h"
#include "clutil.h"
#ifdef CLU_NO_SRC
#include "clcache_bins.h"
#else
#endif

void clu_init(void) {
#ifndef CLU_NO_SRC
  mkdir("/tmp/clcache", 0777);
#endif
}
namespace { // helper functions

std::string get_platform_info(cl_platform_id platform, cl_platform_info param_name) {
  size_t size = 0;
  CL_CHECK(clGetPlatformInfo(platform, param_name, 0, NULL, &size));
  std::string ret;
  ret.resize(size, '\0');
  CL_CHECK(clGetPlatformInfo(platform, param_name, size, &ret[0], NULL));
  return ret;
}
void cl_print_info(cl_platform_id platform, cl_device_id device) {
  std::string info = get_platform_info(platform, CL_PLATFORM_VENDOR);
  printf("vendor: '%s'\n", info.c_str());

  info = get_platform_info(platform, CL_PLATFORM_VERSION);
  printf("platform version: '%s'\n", info.c_str());

  info = get_platform_info(platform, CL_PLATFORM_PROFILE);
  printf("profile: '%s'\n", info.c_str());

  info = get_platform_info(platform, CL_PLATFORM_EXTENSIONS);
  printf("extensions: '%s'\n", info.c_str());

  char str[4096] = {};
  clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(str), str, NULL);
  printf("name: '%s'\n", str);

  clGetDeviceInfo(device, CL_DEVICE_VERSION, sizeof(str), str, NULL);
  printf("device version: '%s'\n", str);

  size_t sz;
  clGetDeviceInfo(device, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(sz), &sz, NULL);
  printf("max work group size: %zu\n", sz);

  cl_device_type type;
  clGetDeviceInfo(device, CL_DEVICE_TYPE, sizeof(type), &type, NULL);
  printf("type = 0x%04x = ", (unsigned int)type);
  switch(type) {
  case CL_DEVICE_TYPE_CPU:
    printf("CL_DEVICE_TYPE_CPU\n");
    break;
  case CL_DEVICE_TYPE_GPU:
    printf("CL_DEVICE_TYPE_GPU\n");
    break;
  case CL_DEVICE_TYPE_ACCELERATOR:
    printf("CL_DEVICE_TYPE_ACCELERATOR\n");
    break;
  default:
    printf("Other...\n" );
    break;
  }
}

void cl_print_build_errors(cl_program program, cl_device_id device) {
  cl_build_status status;
  clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_STATUS,
          sizeof(cl_build_status), &status, NULL);

  size_t log_size;
  clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, NULL, &log_size);

  std::unique_ptr<char[]> log = std::make_unique<char[]>(log_size + 1);
  clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, log_size + 1, &log[0], NULL);
  printf("build failed; status=%d, log:\n%s\n", status, &log[0]);
}

#ifndef CLU_NO_CACHE
cl_program load_cached_program(cl_context ctx, cl_device_id device_id, std::string cache_file) {
  size_t bin_size;
  uint8_t* bin = (uint8_t*)read_file(cache_file.c_str(), &bin_size);
  if (!bin) return NULL;

  cl_program prg = CL_CHECK_ERR(clCreateProgramWithBinary(ctx, 1, &device_id, &bin_size, (const uint8_t**)&bin, NULL, &err));
  free(bin);

  CL_CHECK(clBuildProgram(prg, 1, &device_id, NULL, NULL, NULL));
  return prg;
}

std::vector<uint8_t> get_program_binary(cl_program prg) {
  cl_uint num_devices = 0;
  CL_CHECK(clGetProgramInfo(prg, CL_PROGRAM_NUM_DEVICES, sizeof(num_devices), &num_devices, NULL));
  assert(num_devices == 1);

  size_t binary_size = 0;
  CL_CHECK(clGetProgramInfo(prg, CL_PROGRAM_BINARY_SIZES, sizeof(binary_size), &binary_size, NULL));
  assert(binary_size > 0);

  std::vector<uint8_t> binary_buf(binary_size);
  uint8_t* bufs[] = {&binary_buf[0]};
  CL_CHECK(clGetProgramInfo(prg, CL_PROGRAM_BINARIES, sizeof(bufs), &bufs, NULL));
  return binary_buf;
}

std::string get_cached_path(cl_device_id device_id, const char* src, const char* args,
                            const char* file, int line, const char* function) {
  cl_platform_id platform;
  CL_CHECK(clGetDeviceInfo(device_id, CL_DEVICE_PLATFORM, sizeof(platform), &platform, NULL));
  std::string platform_version = get_platform_info(platform, CL_PLATFORM_VERSION);

  std::string hash_buf = util::string_format("%s%s%d%s%s", platform_version.c_str(), file, line, function, src, args);
  const size_t hash = std::hash<std::string>{}(hash_buf);
  return util::string_format("/tmp/clcache/%016" PRIx64 ".clb", hash);
}
#endif
}  // namespace

cl_device_id cl_get_device_id(cl_device_type device_type) {
  cl_uint num_platforms = 0;
  CL_CHECK(clGetPlatformIDs(0, NULL, &num_platforms));
  std::unique_ptr<cl_platform_id[]> platform_ids = std::make_unique<cl_platform_id[]>(num_platforms);
  CL_CHECK(clGetPlatformIDs(num_platforms, &platform_ids[0], NULL));

  for (size_t i = 0; i < num_platforms; i++) {
    std::string platform_name = get_platform_info(platform_ids[i], CL_PLATFORM_NAME);
    printf("platform[%zu] CL_PLATFORM_NAME: %s\n", i, platform_name.c_str());

    cl_uint num_devices;
    int err = clGetDeviceIDs(platform_ids[i], device_type, 0, NULL, &num_devices);
    if (err != 0 || !num_devices) {
      continue;
    }
    // Get first device
    cl_device_id device_id = NULL;
    CL_CHECK(clGetDeviceIDs(platform_ids[i], device_type, 1, &device_id, NULL));
    cl_print_info(platform_ids[i], device_id);
    return device_id;
  }

  printf("No valid openCL platform found\n");
  assert(0);
  return nullptr;
}

cl_program cl_program_from_string(cl_context ctx, cl_device_id device_id, const char* src, const char* args,
                                  const char* file, int line, const char* function) {
  cl_program prg = NULL;
#ifndef CLU_NO_CACHE
  std::string cache_path = get_cached_path(device_id, src, args, file, line, function);
  prg = load_cached_program(ctx, device_id, cache_path);
#endif
  if (prg == NULL) {
    prg = CL_CHECK_ERR(clCreateProgramWithSource(ctx, 1, (const char**)&src, NULL, &err));

    int err = clBuildProgram(prg, 1, &device_id, args, NULL, NULL);
    if (err != 0) {
      cl_print_build_errors(prg, device_id);
    }
    assert(err == 0);

#ifndef CLU_NO_CACHE
    // write program binary to cache
    std::vector<uint8_t> binary_buf = get_program_binary(prg);
    write_file(cache_path.c_str(), binary_buf.data(), binary_buf.size());
#endif
  }
  return prg;
}

cl_program cl_program_from_file(cl_context ctx, cl_device_id device_id, const char* path, const char* args,
                                const char* file, int line, const char* function) {
  std::string src_buf = util::read_file(path);
  return cl_program_from_string(ctx, device_id, &src_buf[0], args, file, line, function);
}

// Given a cl code and return a string represenation
const char* cl_get_error_string(int err) {
    switch (err) {
        case 0: return "CL_SUCCESS";
        case -1: return "CL_DEVICE_NOT_FOUND";
        case -2: return "CL_DEVICE_NOT_AVAILABLE";
        case -3: return "CL_COMPILER_NOT_AVAILABLE";
        case -4: return "CL_MEM_OBJECT_ALLOCATION_FAILURE";
        case -5: return "CL_OUT_OF_RESOURCES";
        case -6: return "CL_OUT_OF_HOST_MEMORY";
        case -7: return "CL_PROFILING_INFO_NOT_AVAILABLE";
        case -8: return "CL_MEM_COPY_OVERLAP";
        case -9: return "CL_IMAGE_FORMAT_MISMATCH";
        case -10: return "CL_IMAGE_FORMAT_NOT_SUPPORTED";
        case -12: return "CL_MAP_FAILURE";
        case -13: return "CL_MISALIGNED_SUB_BUFFER_OFFSET";
        case -14: return "CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST";
        case -15: return "CL_COMPILE_PROGRAM_FAILURE";
        case -16: return "CL_LINKER_NOT_AVAILABLE";
        case -17: return "CL_LINK_PROGRAM_FAILURE";
        case -18: return "CL_DEVICE_PARTITION_FAILED";
        case -19: return "CL_KERNEL_ARG_INFO_NOT_AVAILABLE";
        case -30: return "CL_INVALID_VALUE";
        case -31: return "CL_INVALID_DEVICE_TYPE";
        case -32: return "CL_INVALID_PLATFORM";
        case -33: return "CL_INVALID_DEVICE";
        case -34: return "CL_INVALID_CONTEXT";
        case -35: return "CL_INVALID_QUEUE_PROPERTIES";
        case -36: return "CL_INVALID_COMMAND_QUEUE";
        case -37: return "CL_INVALID_HOST_PTR";
        case -38: return "CL_INVALID_MEM_OBJECT";
        case -39: return "CL_INVALID_IMAGE_FORMAT_DESCRIPTOR";
        case -40: return "CL_INVALID_IMAGE_SIZE";
        case -41: return "CL_INVALID_SAMPLER";
        case -42: return "CL_INVALID_BINARY";
        case -43: return "CL_INVALID_BUILD_OPTIONS";
        case -44: return "CL_INVALID_PROGRAM";
        case -45: return "CL_INVALID_PROGRAM_EXECUTABLE";
        case -46: return "CL_INVALID_KERNEL_NAME";
        case -47: return "CL_INVALID_KERNEL_DEFINITION";
        case -48: return "CL_INVALID_KERNEL";
        case -49: return "CL_INVALID_ARG_INDEX";
        case -50: return "CL_INVALID_ARG_VALUE";
        case -51: return "CL_INVALID_ARG_SIZE";
        case -52: return "CL_INVALID_KERNEL_ARGS";
        case -53: return "CL_INVALID_WORK_DIMENSION";
        case -54: return "CL_INVALID_WORK_GROUP_SIZE";
        case -55: return "CL_INVALID_WORK_ITEM_SIZE";
        case -56: return "CL_INVALID_GLOBAL_OFFSET";
        case -57: return "CL_INVALID_EVENT_WAIT_LIST";
        case -58: return "CL_INVALID_EVENT";
        case -59: return "CL_INVALID_OPERATION";
        case -60: return "CL_INVALID_GL_OBJECT";
        case -61: return "CL_INVALID_BUFFER_SIZE";
        case -62: return "CL_INVALID_MIP_LEVEL";
        case -63: return "CL_INVALID_GLOBAL_WORK_SIZE";
        case -64: return "CL_INVALID_PROPERTY";
        case -65: return "CL_INVALID_IMAGE_DESCRIPTOR";
        case -66: return "CL_INVALID_COMPILER_OPTIONS";
        case -67: return "CL_INVALID_LINKER_OPTIONS";
        case -68: return "CL_INVALID_DEVICE_PARTITION_COUNT";
        case -69: return "CL_INVALID_PIPE_SIZE";
        case -70: return "CL_INVALID_DEVICE_QUEUE";
        case -71: return "CL_INVALID_SPEC_ID";
        case -72: return "CL_MAX_SIZE_RESTRICTION_EXCEEDED";
        case -1002: return "CL_INVALID_D3D10_DEVICE_KHR";
        case -1003: return "CL_INVALID_D3D10_RESOURCE_KHR";
        case -1004: return "CL_D3D10_RESOURCE_ALREADY_ACQUIRED_KHR";
        case -1005: return "CL_D3D10_RESOURCE_NOT_ACQUIRED_KHR";
        case -1006: return "CL_INVALID_D3D11_DEVICE_KHR";
        case -1007: return "CL_INVALID_D3D11_RESOURCE_KHR";
        case -1008: return "CL_D3D11_RESOURCE_ALREADY_ACQUIRED_KHR";
        case -1009: return "CL_D3D11_RESOURCE_NOT_ACQUIRED_KHR";
        case -1010: return "CL_INVALID_DX9_MEDIA_ADAPTER_KHR";
        case -1011: return "CL_INVALID_DX9_MEDIA_SURFACE_KHR";
        case -1012: return "CL_DX9_MEDIA_SURFACE_ALREADY_ACQUIRED_KHR";
        case -1013: return "CL_DX9_MEDIA_SURFACE_NOT_ACQUIRED_KHR";
        case -1093: return "CL_INVALID_EGL_OBJECT_KHR";
        case -1092: return "CL_EGL_RESOURCE_NOT_ACQUIRED_KHR";
        case -1001: return "CL_PLATFORM_NOT_FOUND_KHR";
        case -1057: return "CL_DEVICE_PARTITION_FAILED_EXT";
        case -1058: return "CL_INVALID_PARTITION_COUNT_EXT";
        case -1059: return "CL_INVALID_PARTITION_NAME_EXT";
        case -1094: return "CL_INVALID_ACCELERATOR_INTEL";
        case -1095: return "CL_INVALID_ACCELERATOR_TYPE_INTEL";
        case -1096: return "CL_INVALID_ACCELERATOR_DESCRIPTOR_INTEL";
        case -1097: return "CL_ACCELERATOR_TYPE_NOT_SUPPORTED_INTEL";
        case -1000: return "CL_INVALID_GL_SHAREGROUP_REFERENCE_KHR";
        case -1098: return "CL_INVALID_VA_API_MEDIA_ADAPTER_INTEL";
        case -1099: return "CL_INVALID_VA_API_MEDIA_SURFACE_INTEL";
        case -1100: return "CL_VA_API_MEDIA_SURFACE_ALREADY_ACQUIRED_INTEL";
        case -1101: return "CL_VA_API_MEDIA_SURFACE_NOT_ACQUIRED_INTEL";
        default: return "CL_UNKNOWN_ERROR";
    }
}
