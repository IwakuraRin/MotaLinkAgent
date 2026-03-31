<template>
  <section class="panel">
    <h2>CAR CAMERA</h2>
    <div class="video-box">
      <!-- 显示小车摄像头画面 -->
      <img :src="cameraUrl" alt="小车摄像头画面" @error="$emit('camera-error')" />
    </div>

    <div class="control-group">
      <label>Car stream URL:</label>
      <!-- 这里直接用 v-model 风格的事件，方便在 App 里绑定 -->
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
  name: 'CameraView_CAR',
  props: {
    // 小车摄像头视频流地址
    cameraUrl: {
      type: String,
      required: true,
    },
  },
  emits: ['update:camera-url', 'camera-error'],
};
</script>
