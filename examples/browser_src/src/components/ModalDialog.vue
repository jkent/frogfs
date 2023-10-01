<script setup>
import { watch } from 'vue';

const props = defineProps({
  show: Boolean,
  type: {
    type: String,
    default: 'cancel-ok',
  },
  enter: {
    type: Boolean,
    default: true,
  }
});
const emit = defineEmits({
  close: null,
  apply: null,
});

function emit_close(e) {
  if (e.keyCode == 13 && props.enter) {
    emit('apply');
  } else if (e.keyCode == 27) {
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
</script>

<template>
  <Transition name="modal-dialog">
    <div v-if="show" class="modal-toplevel">
      <div class="modal-backdrop" @click="$emit('close')"></div>
      <div class="modal-container">
        <div class="modal-header">
          <slot name="header"></slot>
        </div>
        <div class="modal-body">
          <slot name="body"></slot>
        </div>
        <div class="modal-footer">
          <slot name="footer">
            <button
              v-if="type.includes('ok')"
              class="modal-default-button"
              @click="$emit('apply')"
            >OK</button>
            <button
              v-if="type.includes('cancel')"
              class="modal-button"
              @click="$emit('close')"
            >Cancel</button>
          </slot>
        </div>
      </div>
    </div>
  </Transition>
</template>

<style>
.modal-toplevel {
  position: fixed;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  display: flex;
  transition: opacity 0.3s ease;
}

.modal-backdrop {
  position: fixed;
  z-index: 9997;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  background-color: rgba(0, 0, 0, 0.5);
}

.modal-container {
  position: relative;
  z-index: 9998;
  width: 400px;
  margin: auto;
  padding: 1em 1.5em 1em 1em;
  background-color: #fff;
  border-radius: 0.5em;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.33);
}

.modal-header h3 {
  margin-top: 0;
  /* color: #42b983; */
  color: slategray;
}

.modal-dialog-body {
  margin: 20px 0;
}

.modal-body input {
  width: 100%;
}

.modal-body textarea {
  width: 100%;
  height: 10em;
}

.modal-footer {
  padding-top: 0.75em;
}

.modal-button, .modal-default-button {
  margin: 0 -0.625em 0 1.5em;
  padding: 0.5em 1em;
  float: right;
}

.modal-default-button {
  font-weight: bold;
}

/*
 * The following styles are auto-applied to elements with
 * transition="modal-dialog" when their visibility is toggled
 * by Vue.js.
 *
 * You can easily play with the modal transition by editing
 * these styles.
 */

.modal-enter-from {
  opacity: 0;
}

.modal-leave-to {
  opacity: 0;
}

.modal-enter-from .modal-container,
.modal-leave-to .modal-container {
  -webkit-transform: scale(1.1);
  transform: scale(1.1);
}
</style>