<script setup>
import icon_close from './icons/close.svg'
import { watch } from 'vue';

const props = defineProps({
  show: Boolean,
  title: String,
  url: String,
});
const emit = defineEmits({
  close: null,
});

function emit_close(e) {
  if (e.keyCode == 27) {
    emit('close');
  }
}

watch(() => props.show, (value) => {
  if (value) {
    document.addEventListener('keyup', emit_close);
  } else {
    document.removeEventListener('keyup', emit_close);
  }
});

(new Image()).src = icon_close;
</script>

<template>
  <Transition name="modal">
    <div v-if="show" class="modal-toplevel">
      <div class="modal-backdrop" @click="$emit('close')"></div>
      <div class="modal-container modal-iframe-container">
        <div class="modal-header"><h3>{{ title }}</h3></div>
        <iframe :src="url"></iframe>
        <img src="./icons/close.svg" @click="emit('close')">
      </div>
    </div>
  </Transition>
</template>

<style>
.modal-iframe-container {
  width: 85%;
  height: 65%;
  padding: 1em 1em 3em 0em;
}

.modal-iframe-container iframe {
  border: none;
  width: 100%;
  height: 100%;
  margin: 0.5em;
}

.modal-iframe-container img {
  position: absolute;
  z-index: 9999;
  top: 8px;
  right: 8px;
  padding: 0;
}

.modal-iframe-container h3 {
  margin: 0 0.75em;
}
</style>