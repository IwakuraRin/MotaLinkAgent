<template>
  <section class="panel">
    <h2>📷 实时画面</h2>
    <div class="video-box">
      <img :src="cameraUrl" alt="摄像头画面" @error="$emit('camera-error')" />
    </div>

    <div class="control-group">
      <label>视频流地址:</label>
      <input
        type="text"
        :value="cameraUrl"
        @input="$emit('update:cameraUrl', $event.target.value)"
      />
    </div>
  </section>
</template>

<script setup>
defineProps({
  cameraUrl: {
    type: String,
    required: true,
  },
});

defineEmits(['update:cameraUrl', 'camera-error']);
</script>
