<template>
  <section class="panel">
    <h2>ROBOTIC ARM CAMERA</h2>
    <div class="video-box">
      <!-- 显示机械臂摄像头画面 -->
      <img :src="cameraUrl" alt="机械臂摄像头画面" @error="$emit('camera-error')" />
    </div>

    <div class="control-group">
      <label>Arm stream URL:</label>
      <input
        type="text"
        :value="cameraUrl"
        @input="$emit('update:camera-url', $event.target.value)"
      />
    </div>
  </section>
</template>

<script>
export default {
  name: 'CameraView_RoboticArm',
  props: {
    // 机械臂摄像头视频流地址
    cameraUrl: {
      type: String,
      required: true,
    },
  },
  emits: ['update:camera-url', 'camera-error'],
};
</script>