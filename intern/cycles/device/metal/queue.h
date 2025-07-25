/* SPDX-FileCopyrightText: 2021-2022 Blender Foundation
 *
 * SPDX-License-Identifier: Apache-2.0 */

#pragma once

#ifdef WITH_METAL

#  include "device/kernel.h"
#  include "device/memory.h"
#  include "device/queue.h"

#  include "device/metal/util.h"
#  include "kernel/device/metal/globals.h"

#  define MAX_SAMPLE_BUFFER_LENGTH 4096

/* The number of resources to be contiguously encoded into the MetalAncillaries struct. */
#  define ANCILLARY_SLOT_COUNT 11

CCL_NAMESPACE_BEGIN

class MetalDevice;

/* Base class for Metal queues. */
class MetalDeviceQueue : public DeviceQueue {
 public:
  MetalDeviceQueue(MetalDevice *device);
  ~MetalDeviceQueue() override;

  int num_concurrent_states(const size_t /*state_size*/) const override;
  int num_concurrent_busy_states(const size_t /*state_size*/) const override;
  int num_sort_partitions(int max_num_paths, uint max_scene_shaders) const override;
  bool supports_local_atomic_sort() const override;

  void init_execution() override;

  bool enqueue(DeviceKernel kernel,
               const int work_size,
               const DeviceKernelArguments &args) override;

  bool synchronize() override;

  void zero_to_device(device_memory &mem) override;
  void copy_to_device(device_memory &mem) override;
  void copy_from_device(device_memory &mem) override;

  void *native_queue() override;

  unique_ptr<DeviceGraphicsInterop> graphics_interop_create() override;

 protected:
  void setup_capture();
  void update_capture(DeviceKernel kernel);
  void begin_capture();
  void end_capture();
  void prepare_resources(DeviceKernel kernel);

  id<MTLComputeCommandEncoder> get_compute_encoder(DeviceKernel kernel);
  id<MTLBlitCommandEncoder> get_blit_encoder();

  MetalDevice *metal_device_;

  API_AVAILABLE(macos(11.0), ios(14.0))
  MTLCommandBufferDescriptor *command_buffer_desc_ = nullptr;
  id<MTLDevice> mtlDevice_ = nil;
  id<MTLCommandQueue> mtlCommandQueue_ = nil;
  id<MTLCommandBuffer> mtlCommandBuffer_ = nil;
  id<MTLComputeCommandEncoder> mtlComputeEncoder_ = nil;
  id<MTLBlitCommandEncoder> mtlBlitEncoder_ = nil;
  API_AVAILABLE(macos(10.14), ios(14.0))
  id<MTLSharedEvent> shared_event_ = nil;
  API_AVAILABLE(macos(10.14), ios(14.0))
  MTLSharedEventListener *shared_event_listener_ = nil;
  MetalDispatchPipeline active_pipelines_[DEVICE_KERNEL_NUM];

  dispatch_queue_t event_queue_;
  dispatch_semaphore_t wait_semaphore_;

  uint64_t shared_event_id_;
  uint64_t command_buffers_submitted_ = 0;
  uint64_t command_buffers_completed_ = 0;
  Stats &stats_;

  void close_compute_encoder();
  void close_blit_encoder();

  bool verbose_tracing_ = false;
  bool label_command_encoders_ = false;

  /* Per-kernel profiling (see CYCLES_METAL_PROFILING). */

  struct TimingData {
    DeviceKernel kernel;
    int work_size;
    uint64_t timing_id;
  };
  std::vector<TimingData> command_encoder_labels_;
  bool profiling_enabled_ = false;
  uint64_t current_encoder_idx_ = 0;

  std::atomic<uint64_t> counter_sample_buffer_curr_idx_ = 0;

  void flush_timing_stats();

  struct TimingStats {
    double total_time = 0.0;
    uint64_t total_work_size = 0;
    uint64_t num_dispatches = 0;
  };
  TimingStats timing_stats_[DEVICE_KERNEL_NUM];
  double last_completion_time_ = 0.0;

  /* .gputrace capture (see CYCLES_DEBUG_METAL_CAPTURE_...). */

  id<MTLCaptureScope> mtlCaptureScope_ = nil;
  DeviceKernel capture_kernel_;
  int capture_dispatch_counter_ = 0;
  bool capture_samples_ = false;
  int capture_reset_counter_ = 0;
  bool is_capturing_ = false;
  bool is_capturing_to_disk_ = false;
  bool has_captured_to_disk_ = false;
};

CCL_NAMESPACE_END

#endif /* WITH_METAL */
