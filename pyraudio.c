/* raudio.c -- https://github.com/takeiteasy/pyraudio
 
 Copyright 2025 George Watson
 
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE. */

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#define TRACELOG(level, ...)
#include "raudio.c"

typedef struct {
    PyObject_HEAD
    Wave wave;
} rWave;

static int rWave_Init(rWave *self, PyObject *args, PyObject *kwds) {
    char *path = NULL;  // Initialize to NULL
    static char *kwlist[] = {"path", NULL}; // Keyword arguments
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|s", kwlist, &path))
        return -1;
    if (path) {
        self->wave = LoadWave(path);
        if (!IsWaveReady(self->wave))
            return -1;
    }
    return 0;
}

static void rWave_Dealloc(rWave* self) {
    if (IsWaveReady(self->wave))
        UnloadWave(self->wave);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static Py_ssize_t rWave_length(rWave *self) {
    float seconds = (float)self->wave.frameCount*1000.0/(self->wave.sampleRate*self->wave.channels);
    return (Py_ssize_t)seconds;
}

static PyMappingMethods rWave_mapping = {
    (lenfunc)rWave_length, NULL, NULL
};

static PyObject* rWave_is_ready(rWave *self, PyObject *args) {
    if (IsWaveReady(self->wave)) {
        Py_RETURN_TRUE;
    } else {
        Py_RETURN_FALSE;
    }
}

static PyObject* rWave_export(rWave *self, PyObject *args, PyObject *kwds) {
    char *path = NULL;  // Initialize to NULL
    static char *kwlist[] = {"path", NULL}; // Keyword arguments
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|s", kwlist, &path)) {
        PyErr_SetString(PyExc_RuntimeError, "No path passed to export");
        return NULL;
    }
    if (!ExportWave(self->wave, path)) {
        PyErr_Format(PyExc_OSError, "Failed to write to file \"%s\"", path);
        return NULL;
    }
    Py_RETURN_TRUE;
}

static PyObject* rWave_copy(rWave *self, PyObject *args, PyObject *kwds) {
    if (!IsWaveReady(self->wave)) {
        PyErr_SetString(PyExc_RuntimeError, "Cannot copy uninitialized Wave");
        return NULL;
    }
    rWave* copy = (rWave*)self->ob_base.ob_type->tp_alloc(self->ob_base.ob_type, 0);
    if (!copy)
        return PyErr_NoMemory();
    memset(&copy->wave, 0, sizeof(Wave));
    copy->wave = WaveCopy(self->wave);
    return (PyObject*)copy;
}

static int rWave_getbuffer(PyObject *obj, Py_buffer *view, int flags) {
    if (!view) {
        PyErr_SetString(PyExc_ValueError, "NULL view in getbuffer");
        return -1;
    }
    rWave *self = (rWave*)obj;
    view->obj = (PyObject*)self;
    view->buf = self->wave.data;
    view->len = self->wave.frameCount * self->wave.channels * sizeof(short);
    view->readonly = 0;
    view->itemsize = sizeof(short);
    view->format = "B";
    view->ndim = 1;
    view->shape = NULL;
    view->strides = NULL;
    view->suboffsets = NULL;
    view->internal = NULL;
    Py_INCREF(self);
    return 0;
}

static PyBufferProcs rWave_as_buffer = {
  (getbufferproc)rWave_getbuffer,
  (releasebufferproc)0,
};

static PyObject* rWave_update(rWave *self, PyObject *original) {
    Py_buffer buffer;
    if (PyObject_GetBuffer(original, &buffer, PyBUF_WRITABLE) == -1)
        return NULL;
    self->wave.data = buffer.buf;
    self->wave.frameCount = buffer.len / (self->wave.channels * sizeof(short));
    PyBuffer_Release(&buffer);
    return Py_BuildValue("d", 1);
}

static PyObject* rWave_samples(rWave *self, PyObject *args) {
    float *samples = LoadWaveSamples(self->wave);
    if (!samples)
        return PyErr_NoMemory();
    int length = self->wave.frameCount * self->wave.channels;
    PyObject *list = PyList_New(length);  // Create a new Python list
    if (!list) {
        free(samples);
        return PyErr_NoMemory();
    }
    
    for (int i = 0; i < length; i++) {
        PyObject *float_obj = PyFloat_FromDouble((double)samples[i]);
        if (!float_obj) {
            for (int j = 0; j < i; j++)
                Py_DECREF(PyList_GetItem(list,j));
            Py_DECREF(list);
            free(samples);
            return NULL;
        }
        int result = PyList_SetItem(list, i, float_obj);
        if (result != 0) {
            Py_DECREF(float_obj);
            for (int j = 0; j <= i; j++)
                Py_DECREF(PyList_GetItem(list,j));
            Py_DECREF(list);
            free(samples);
            return NULL;
        }
    }
    UnloadWaveSamples(samples);
    return list;
}

static PyObject* rWave_crop(rWave *self, PyObject *args) {
    unsigned int initSample, finalSample;
    if (!PyArg_ParseTuple(args, "ii", &initSample, &finalSample)) {
        return NULL;
    }
    if (initSample < 0 || finalSample < 0 || initSample >= finalSample ||
        initSample >= self->wave.frameCount || finalSample > self->wave.frameCount) {
        PyErr_SetString(PyExc_ValueError, "Invalid sample range for crop");
        return NULL;
    }
    WaveCrop(&self->wave, initSample, finalSample);
    Py_RETURN_NONE;
}

static PyObject* rWave_format(rWave *self, PyObject *args) {
    int sampleRate, sampleSize, channels;
    if (!PyArg_ParseTuple(args, "iii", &sampleRate, &sampleSize, &channels)) {
        return NULL;
    }
    if (sampleRate <= 0 || sampleSize <= 0 || channels <= 0) {
        PyErr_SetString(PyExc_ValueError, "Invalid format parameters");
        return NULL;
    }
    WaveFormat(&self->wave, sampleRate, sampleSize, channels);
    Py_RETURN_NONE;
}

static PyMethodDef rWave_methods[] = {
    {"is_ready", (PyCFunction)rWave_is_ready, METH_VARARGS, "Is Wave ready?"},
    {"export", (PyCFunction)rWave_export, METH_VARARGS, "Export Wave to file"},
    {"copy", (PyCFunction)rWave_copy, METH_VARARGS, "Clone a Wave object"},
    {"crop", (PyCFunction)rWave_crop, METH_VARARGS, "Crop a Wave to defined samples range"},
    {"format", (PyCFunction)rWave_format, METH_VARARGS, "Convert Wave data to desired format"},
    {"update", (PyCFunction)rWave_update, METH_O, "Set memory pointer from a buffer"},
    {"samples", (PyCFunction)rWave_samples, METH_VARARGS, "Load samples data from wave as a floats array"},
    {NULL, NULL, 0, NULL}
};

static PyObject* rWave_get_frame_count(rWave *self, void *closure) {
    return PyLong_FromLong(self->wave.frameCount);
}

static PyObject* rWave_get_sample_rate(rWave *self, void *closure) {
    return PyLong_FromLong(self->wave.sampleRate);
}

static PyObject* rWave_get_sample_size(rWave *self, void *closure) {
    return PyLong_FromLong(self->wave.sampleSize);
}

static PyObject* rWave_get_channels(rWave *self, void *closure) {
    return PyLong_FromLong(self->wave.channels);
}

static PyGetSetDef rWave_attrs[] = {
    {"frame_count", (getter)rWave_get_frame_count, NULL, "Total number of frames (considering channels)", NULL},
    {"sample_rate", (getter)rWave_get_sample_rate, NULL, "Frequency (samples per second)", NULL},
    {"sample_size", (getter)rWave_get_sample_size, NULL, "Bit depth (bits per sample): 8, 16, 32 (24 not supported)", NULL},
    {"channels", (getter)rWave_get_channels, NULL, "Number of channels (1-mono, 2-stereo, ...)", NULL},
    {NULL}
};

static PyTypeObject rWaveType = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "raudio.Wave",
    .tp_basicsize = sizeof(rWave),
    .tp_dealloc = (destructor)rWave_Dealloc,
    .tp_as_mapping = &rWave_mapping,
    .tp_as_buffer = &rWave_as_buffer,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = PyDoc_STR("Wave object"),
    .tp_methods = rWave_methods,
    .tp_getset = rWave_attrs,
    .tp_init = (initproc)rWave_Init,
    .tp_alloc = PyType_GenericAlloc,
    .tp_new = PyType_GenericNew,
};

typedef struct {
    PyObject_HEAD
    AudioStream stream;
} rAudioStream;

static int rAudioStream_Init(rAudioStream *self, PyObject *args, PyObject *kwds) {
    unsigned int sampleRate, sampleSize, channels;
    if (!PyArg_ParseTuple(args, "iii", &sampleRate, &sampleSize, &channels)) {
        PyErr_SetString(PyExc_ValueError, "Invalid parameters");
        return -1;
    }
    self->stream = LoadAudioStream(sampleRate, sampleSize, channels);
    if (!IsAudioStreamReady(self->stream))
        return -1;
    return 0;
}

static void rAudioStream_Dealloc(rAudioStream* self) {
    if (IsAudioStreamReady(self->stream))
        UnloadAudioStream(self->stream);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject* rAudioStream_is_ready(rAudioStream *self, PyObject *args) {
    if (IsAudioStreamReady(self->stream)) {
        Py_RETURN_TRUE;
    } else {
        Py_RETURN_FALSE;
    }
}

static PyObject* rAudioStream_play(rAudioStream *self, PyObject *args) {
    if (!IsAudioStreamReady(self->stream)) {
        PyErr_SetString(PyExc_RuntimeError, "AudioStream is not ready");
        return NULL;
    }
    PlayAudioStream(self->stream);
    Py_RETURN_NONE;
}

static PyObject* rAudioStream_stop(rAudioStream *self, PyObject *args) {
    if (!IsAudioStreamReady(self->stream)) {
        PyErr_SetString(PyExc_RuntimeError, "AudioStream is not ready");
        return NULL;
    }
    StopAudioStream(self->stream);
    Py_RETURN_NONE;
}

static PyObject* rAudioStream_pause(rAudioStream *self, PyObject *args) {
    if (!IsAudioStreamReady(self->stream)) {
        PyErr_SetString(PyExc_RuntimeError, "AudioStream is not ready");
        return NULL;
    }
    PauseAudioStream(self->stream);
    Py_RETURN_NONE;
}

static PyObject* rAudioStream_resume(rAudioStream *self, PyObject *args) {
    if (!IsAudioStreamReady(self->stream)) {
        PyErr_SetString(PyExc_RuntimeError, "AudioStream is not ready");
        return NULL;
    }
    ResumeAudioStream(self->stream);
    Py_RETURN_NONE;
}

static PyObject* rAudioStream_is_playing(rAudioStream *self, PyObject *args) {
    if (!IsAudioStreamReady(self->stream)) {
        PyErr_SetString(PyExc_RuntimeError, "AudioStream is not ready");
        return NULL;
    }
    if (IsAudioStreamPlaying(self->stream)) {
        Py_RETURN_TRUE;
    } else {
        Py_RETURN_FALSE;
    }
}

static PyObject* rAudioStream_update(rAudioStream *self, PyObject *buf) {
    Py_buffer buffer;
    if (PyObject_GetBuffer(buf, &buffer, PyBUF_WRITABLE) == -1)
        return NULL;
    UpdateAudioStream(self->stream, buffer.buf, buffer.len / (self->stream.channels * sizeof(short)));
    PyBuffer_Release(&buffer);
    return Py_BuildValue("d", 1);
}

static PyMethodDef rAudioStream_methods[] = {
    {"is_ready", (PyCFunction)rAudioStream_is_ready, METH_VARARGS, "Is AudioStream ready?"},
    {"play", (PyCFunction)rAudioStream_play, METH_VARARGS, "Play a AudioStream" },
    {"stop", (PyCFunction)rAudioStream_stop, METH_VARARGS, "Stop playing a AudioStream" },
    {"pause", (PyCFunction)rAudioStream_pause, METH_VARARGS, "Pause a AudioStream" },
    {"resume", (PyCFunction)rAudioStream_resume, METH_VARARGS, "Resume a paused AudioStream" },
    {"is_playing", (PyCFunction)rAudioStream_is_playing, METH_VARARGS, "Check if a AudioStream is currently playing" },
    {"update", (PyCFunction)rAudioStream_update, METH_VARARGS, "Update AudioStream with a new buffer"},
    {NULL}
};

static PyObject* rAudioStream_get_sample_rate(rAudioStream *self, void *closure) {
    return PyLong_FromUnsignedLong(self->stream.sampleRate);
}

static PyObject* rAudioStream_get_sample_size(rAudioStream *self, void *closure) {
    return PyLong_FromUnsignedLong(self->stream.sampleSize);
}

static PyObject* rAudioStream_get_channels(rAudioStream *self, void *closure) {
    return PyLong_FromUnsignedLong(self->stream.channels);
}

static PyGetSetDef rAudioStream_attrs[] = {
    {"sample_rate", (getter)rAudioStream_get_sample_rate, NULL, "AudioStream sample rate", NULL},
    {"sample_size", (getter)rAudioStream_get_sample_size, NULL, "AudioStream sample size", NULL},
    {"channels", (getter)rAudioStream_get_channels, NULL, "AudioStream channels", NULL},
    {NULL}
};

static PyTypeObject rAudioStreamType = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "raudio.AudioStream",
    .tp_basicsize = sizeof(rAudioStream),
    .tp_dealloc = (destructor)rAudioStream_Dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = PyDoc_STR("AudioStream object"),
    .tp_methods = rAudioStream_methods,
    .tp_getset = rAudioStream_attrs,
    .tp_init = (initproc)rAudioStream_Init,
    .tp_alloc = PyType_GenericAlloc,
    .tp_new = PyType_GenericNew,
};

typedef struct {
    PyObject_HEAD
    Sound sound;
    double volume;
    double pitch;
    double pan;
} rSound;

static int rSound_Init(rSound *self, PyObject *args, PyObject *kwds) {
    PyObject *source = NULL;  // Could be a string or a Wave object
    static char *kwlist[] = {"path", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|O", kwlist, &source, NULL)) // "O|O" for optional object
        return -1;
    if (!source) {
        PyErr_SetString(PyExc_TypeError, "Must provide a path or Wave object");
        return -1;
    }
    
    if (PyUnicode_Check(source)) {
        const char *path = PyUnicode_AsUTF8(source);
        if (!path)
            return -1;
        self->sound = LoadSound(path);
        if (!IsSoundReady(self->sound))
            return -1;
    } else if (PyObject_TypeCheck(source, &rWaveType)) {
        rWave *wave_obj = (rWave*)source;
        self->sound = LoadSoundFromWave(wave_obj->wave);
        if (!IsSoundReady(self->sound))
            return -1;
    } else {
        PyErr_SetString(PyExc_TypeError, "Source must be a path (string) or a Wave object");
        return -1;
    }
    self->volume = 1.0;
    self->pitch = 1.0;
    self->pan = 0.5;
    return 0;
}

static void rSound_Dealloc(rSound* self) {
    if (IsSoundReady(self->sound))
        UnloadSound(self->sound);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static Py_ssize_t rSound_length(rSound *self) {
    float seconds = (float)self->sound.frameCount*1000.0/(self->sound.stream.sampleRate*self->sound.stream.channels);
    return (Py_ssize_t)seconds;
}

static PyMappingMethods rSound_mapping = {
    (lenfunc)rSound_length, NULL, NULL
};

static PyObject* rSound_is_ready(rSound *self, PyObject *args) {
    if (IsSoundReady(self->sound)) {
        Py_RETURN_TRUE;
    } else {
        Py_RETURN_FALSE;
    }
}

static PyObject* rSound_play(rSound *self, PyObject *args) {
    if (!IsSoundReady(self->sound)) {
        PyErr_SetString(PyExc_RuntimeError, "Sound is not ready");
        return NULL;
    }
    PlaySound(self->sound);
    Py_RETURN_NONE;
}

static PyObject* rSound_stop(rSound *self, PyObject *args) {
    if (!IsSoundReady(self->sound)) {
        PyErr_SetString(PyExc_RuntimeError, "Sound is not ready");
        return NULL;
    }
    StopSound(self->sound);
    Py_RETURN_NONE;
}

static PyObject* rSound_pause(rSound *self, PyObject *args) {
    if (!IsSoundReady(self->sound)) {
        PyErr_SetString(PyExc_RuntimeError, "Sound is not ready");
        return NULL;
    }
    PauseSound(self->sound);
    Py_RETURN_NONE;
}

static PyObject* rSound_resume(rSound *self, PyObject *args) {
    if (!IsSoundReady(self->sound)) {
        PyErr_SetString(PyExc_RuntimeError, "Sound is not ready");
        return NULL;
    }
    ResumeSound(self->sound);
    Py_RETURN_NONE;
}

static PyObject* rSound_is_playing(rSound *self, PyObject *args) {
    if (!IsSoundReady(self->sound)) {
        PyErr_SetString(PyExc_RuntimeError, "Sound is not ready");
        return NULL;
    }
    if (IsSoundPlaying(self->sound)) {
        Py_RETURN_TRUE;
    } else {
        Py_RETURN_FALSE;
    }
}

static PyObject* rSound_update(rSound *self, PyObject *buf) {
    Py_buffer buffer;
    if (PyObject_GetBuffer(buf, &buffer, PyBUF_WRITABLE) == -1)
        return NULL;
    UpdateSound(self->sound, buffer.buf, buffer.len / (self->sound.stream.channels * sizeof(short)));
    PyBuffer_Release(&buffer);
    return Py_BuildValue("d", 1);
}

static PyMethodDef rSound_methods[] = {
    {"is_ready", (PyCFunction)rSound_is_ready, METH_VARARGS, "Is Sound ready?"},
    {"play", (PyCFunction)rSound_play, METH_VARARGS, "Play a Sound" },
    {"stop", (PyCFunction)rSound_stop, METH_VARARGS, "Stop playing a Sound" },
    {"pause", (PyCFunction)rSound_pause, METH_VARARGS, "Pause a Sound" },
    {"resume", (PyCFunction)rSound_resume, METH_VARARGS, "Resume a paused Sound" },
    {"is_playing", (PyCFunction)rSound_is_playing, METH_VARARGS, "Check if a Sound is currently playing" },
    {"update", (PyCFunction)rSound_update, METH_VARARGS, "Update a Sound with a new buffer" },
    {NULL, NULL, 0, NULL}
};

static PyObject* rSound_get_frame_count(rSound *self, void *closure) {
    return PyLong_FromLong(self->sound.frameCount);
}

static PyObject* rSound_get_volume(rSound *self, void *closure) {
    return PyFloat_FromDouble(self->volume);
}

static int rSound_set_volume(rSound *self, PyObject *value, void *closure) {
    if (!value) {
        PyErr_SetString(PyExc_TypeError, "Cannot delete the 'volume' attribute");
        return -1;
    }
    double vol = PyFloat_AsDouble(value);
    if (PyErr_Occurred())
        return -1;
    self->volume = vol;
    SetSoundVolume(self->sound, (float)vol);
    return 0;
}

static PyObject* rSound_get_pitch(rSound *self, void *closure) {
    return PyFloat_FromDouble(self->pitch);
}

static int rSound_set_pitch(rSound *self, PyObject *value, void *closure) {
    if (!value) {
        PyErr_SetString(PyExc_TypeError, "Cannot delete the 'pitch' attribute");
        return -1;
    }
    double pitch = PyFloat_AsDouble(value);
    if (PyErr_Occurred())
        return -1;
    self->pitch = pitch;
    SetSoundPitch(self->sound, (float)pitch);
    return 0;
}

static PyObject* rSound_get_pan(rSound *self, void *closure) {
    return PyFloat_FromDouble(self->pan);
}

static int rSound_set_pan(rSound *self, PyObject *value, void *closure) {
    if (!value) {
        PyErr_SetString(PyExc_TypeError, "Cannot delete the 'pan' attribute");
        return -1;
    }
    double pan = PyFloat_AsDouble(value);
    if (PyErr_Occurred())
        return -1;
    self->pan = pan;
    SetSoundPan(self->sound, (float)pan);
    return 0;
}

static PyObject* rSound_get_stream(rSound *self, void *closure) {
    rAudioStream *stream = (rAudioStream*)rAudioStreamType.tp_alloc(&rAudioStreamType, 0);
    if (!stream)
        return PyErr_NoMemory();
    memcpy(&self->sound.stream, &stream->stream, sizeof(AudioStream));
    return (PyObject*)stream;
}

static int rSound_set_stream(rSound *self, PyObject *value, void *closure) {
    rAudioStream *stream = (rAudioStream*)value;
    if (!PyObject_TypeCheck(value, &rAudioStreamType))
        return -1;
    memcpy(&stream->stream, &self->sound.stream, sizeof(AudioStream));
    Py_DECREF(stream);
    return 0;
}

static PyGetSetDef rSound_attrs[] = {
    {"frame_count", (getter)rSound_get_frame_count, NULL, "Total number of frames (considering channels)", NULL},
    {"volume", (getter)rSound_get_volume, (setter)rSound_set_volume, "Sound volume", NULL},
    {"pitch", (getter)rSound_get_pitch, (setter)rSound_set_pitch, "Sound pitch", NULL},
    {"pan", (getter)rSound_get_pan, (setter)rSound_set_pan, "Sound pan", NULL},
    {"stream", (getter)rSound_get_stream, (setter)rSound_set_stream, "AudioSteam object", NULL},
    {NULL}
};

static PyTypeObject rSoundType = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "raudio.Sound",
    .tp_basicsize = sizeof(rSound),
    .tp_dealloc = (destructor)rSound_Dealloc,
    .tp_as_mapping = &rSound_mapping,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = PyDoc_STR("Sound object"),
    .tp_methods = rSound_methods,
    .tp_getset = rSound_attrs,
    .tp_init = (initproc)rSound_Init,
    .tp_alloc = PyType_GenericAlloc,
    .tp_new = PyType_GenericNew,
};

// Music, audio stream, anything longer than ~10 seconds should be streamed
typedef struct {
    PyObject_HEAD
    Music music;
    double volume;
    double pitch;
    double pan;
} rMusic;

static int rMusic_Init(rMusic *self, PyObject *args, PyObject *kwds) {
    char *path = NULL;  // Initialize to NULL
    static char *kwlist[] = {"path", NULL}; // Keyword arguments
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|s", kwlist, &path))
        return -1;
    if (path) {
        self->music = LoadMusicStream(path);
        if (!IsMusicReady(self->music))
            return -1;
    }
    self->volume = 1.0;
    self->pitch = 1.0;
    self->pan = 0.5;
    return 0;
}

static void rMusic_Dealloc(rMusic* self) {
    if (IsMusicReady(self->music))
        UnloadMusicStream(self->music);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static Py_ssize_t rMusic_length(rMusic *self) {
    return (Py_ssize_t)GetMusicTimeLength(self->music);
}

static PyMappingMethods rMusic_mapping = {
    (lenfunc)rMusic_length, NULL, NULL
};

static PyObject* rMusic_is_ready(rMusic *self, PyObject *args) {
    if (IsMusicReady(self->music)) {
        Py_RETURN_TRUE;
    } else {
        Py_RETURN_FALSE;
    }
}

static PyObject* rMusic_play(rMusic *self, PyObject *args) {
    if (!IsMusicReady(self->music)) {
        PyErr_SetString(PyExc_RuntimeError, "Music is not ready");
        return NULL;
    }
    PlayMusicStream(self->music);
    Py_RETURN_NONE;
}

static PyObject* rMusic_stop(rMusic *self, PyObject *args) {
    if (!IsMusicReady(self->music)) {
        PyErr_SetString(PyExc_RuntimeError, "Music is not ready");
        return NULL;
    }
    StopMusicStream(self->music);
    Py_RETURN_NONE;
}

static PyObject* rMusic_pause(rMusic *self, PyObject *args) {
    if (!IsMusicReady(self->music)) {
        PyErr_SetString(PyExc_RuntimeError, "Music is not ready");
        return NULL;
    }
    PauseMusicStream(self->music);
    Py_RETURN_NONE;
}

static PyObject* rMusic_resume(rMusic *self, PyObject *args) {
    if (!IsMusicReady(self->music)) {
        PyErr_SetString(PyExc_RuntimeError, "Music is not ready");
        return NULL;
    }
    ResumeMusicStream(self->music);
    Py_RETURN_NONE;
}

static PyObject* rMusic_is_playing(rMusic *self, PyObject *args) {
    if (!IsMusicReady(self->music)) {
        PyErr_SetString(PyExc_RuntimeError, "Music is not ready");
        return NULL;
    }
    if (IsMusicStreamPlaying(self->music)) {
        Py_RETURN_TRUE;
    } else {
        Py_RETURN_FALSE;
    }
}

static PyObject* rMusic_seek(rMusic *self, PyObject *args) {
    if (!IsMusicReady(self->music)) {
        PyErr_SetString(PyExc_RuntimeError, "Music is not ready");
        return NULL;
    }
    float offset;
    if (!PyArg_ParseTuple(args, "f", &offset)) {
        PyErr_SetString(PyExc_TypeError, "Seek offset must be a float");
        return NULL;
    }
    if (offset < 0.0)
        offset = 0.0;
    SeekMusicStream(self->music, offset);
    Py_RETURN_NONE;
}

static PyObject* rMusic_update(rMusic *self, PyObject *args) {
    if (!IsMusicReady(self->music)) {
        PyErr_SetString(PyExc_RuntimeError, "Music is not ready");
        return NULL;
    }
    UpdateMusicStream(self->music);
    Py_RETURN_NONE;
}

static PyMethodDef rMusic_methods[] = {
    {"is_ready", (PyCFunction)rMusic_is_ready, METH_VARARGS, "Is Music ready?"},
    {"play", (PyCFunction)rMusic_play, METH_VARARGS, "Play a Music" },
    {"stop", (PyCFunction)rMusic_stop, METH_VARARGS, "Stop playing a Music" },
    {"pause", (PyCFunction)rMusic_pause, METH_VARARGS, "Pause a Music" },
    {"resume", (PyCFunction)rMusic_resume, METH_VARARGS, "Resume a paused Music" },
    {"seek", (PyCFunction)rMusic_seek, METH_VARARGS, "Seek Music to a position (in seconds)" },
    {"is_playing", (PyCFunction)rMusic_is_playing, METH_VARARGS, "Check if a Music is currently playing" },
    {"update", (PyCFunction)rMusic_update, METH_VARARGS, "Update (re-fill) Music buffers if data already processed"},
    {NULL, NULL, 0, NULL}
};

static PyObject* rMusic_get_frame_count(rMusic *self, void *closure) {
    return PyLong_FromLong(self->music.frameCount);
}

static PyObject* rMusic_get_volume(rMusic *self, void *closure) {
    return PyFloat_FromDouble(self->volume);
}

static int rMusic_set_volume(rMusic *self, PyObject *value, void *closure) {
    if (!value) {
        PyErr_SetString(PyExc_TypeError, "Cannot delete the 'volume' attribute");
        return -1;
    }
    double vol = PyFloat_AsDouble(value);
    if (PyErr_Occurred())
        return -1;
    self->volume = vol;
    SetMusicVolume(self->music, (float)vol);
    return 0;
}

static PyObject* rMusic_get_pitch(rMusic *self, void *closure) {
    return PyFloat_FromDouble(self->pitch);
}

static int rMusic_set_pitch(rMusic *self, PyObject *value, void *closure) {
    if (!value) {
        PyErr_SetString(PyExc_TypeError, "Cannot delete the 'pitch' attribute");
        return -1;
    }
    double pitch = PyFloat_AsDouble(value);
    if (PyErr_Occurred())
        return -1;
    self->pitch = pitch;
    SetMusicPitch(self->music, (float)pitch);
    return 0;
}

static PyObject* rMusic_get_pan(rMusic *self, void *closure) {
    return PyFloat_FromDouble(self->pan);
}

static int rMusic_set_pan(rMusic *self, PyObject *value, void *closure) {
    if (!value) {
        PyErr_SetString(PyExc_TypeError, "Cannot delete the 'pan' attribute");
        return -1;
    }
    double pan = PyFloat_AsDouble(value);
    if (PyErr_Occurred())
        return -1;
    self->pan = pan;
    SetMusicPan(self->music, (float)pan);
    return 0;
}

static PyObject* rMusic_get_position(rMusic *self, void *closure) {
    PyObject *obj = PyFloat_FromDouble(IsMusicReady(self->music) ? GetMusicTimePlayed(self->music) : 0.0);
    if (!obj)
        return PyErr_NoMemory();
    return obj;
}

static int rMusic_set_position(rMusic *self, PyObject *value, void *closure) {
    if (!value) {
        PyErr_SetString(PyExc_TypeError, "Cannot delete the 'position' attribute");
        return -1;
    }
    if (PyErr_Occurred())
        return -1;
    SeekMusicStream(self->music, (float)PyFloat_AsDouble(value));
    return 0;
}

static PyObject* rMusic_get_looping(rMusic *self, void *closure) {
    if (self->music.looping) {
        Py_RETURN_TRUE;
    } else {
        Py_RETURN_FALSE;
    }
}

static int rMusic_set_looping(rMusic *self, PyObject *value, void *closure) {
    if (!value) {
        PyErr_SetString(PyExc_TypeError, "Cannot delete the 'loop' attribute");
        return -1;
    }
    if (PyBool_Check(value))
        self->music.looping = value == Py_True;
    else {
        PyErr_SetString(PyExc_TypeError, "Loop attribute must be a boolean");
        return -1;
    }
    return 0;
}

static PyObject* rMusic_get_stream(rMusic *self, void *closure) {
    rAudioStream *stream = (rAudioStream*)rAudioStreamType.tp_alloc(&rAudioStreamType, 0);
    if (!stream)
        return PyErr_NoMemory();
    memcpy(&self->music.stream, &stream->stream, sizeof(AudioStream));
    return (PyObject*)stream;
}

static int rMusic_set_stream(rMusic *self, PyObject *value, void *closure) {
    rAudioStream *stream = (rAudioStream*)value;
    if (!PyObject_TypeCheck(value, &rAudioStreamType))
        return -1;
    memcpy(&stream->stream, &self->music.stream, sizeof(AudioStream));
    Py_DECREF(stream);
    return 0;
}

static PyGetSetDef rMusic_attrs[] = {
    {"frame_count", (getter)rMusic_get_frame_count, NULL, "Total number of frames (considering channels)", NULL},
    {"volume", (getter)rMusic_get_volume, (setter)rMusic_set_volume, "Music volume", NULL},
    {"pitch", (getter)rMusic_get_pitch, (setter)rMusic_set_pitch, "Music pitch", NULL},
    {"pan", (getter)rMusic_get_pan, (setter)rMusic_set_pan, "Music pan", NULL},
    {"position", (getter)rMusic_get_position, (setter)rMusic_set_position, "Music position", NULL},
    {"loop", (getter)rMusic_get_looping, (setter)rMusic_set_looping, "Music looping", NULL},
    {"stream", (getter)rMusic_get_stream, (setter)rMusic_set_stream, "AudioStream object", NULL},
    {NULL}
};

static PyTypeObject rMusicType = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "raudio.Music",
    .tp_basicsize = sizeof(rMusic),
    .tp_dealloc = (destructor)rMusic_Dealloc,
    .tp_as_mapping = &rMusic_mapping,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = PyDoc_STR("Music object"),
    .tp_methods = rMusic_methods,
    .tp_getset = rMusic_attrs,
    .tp_init = (initproc)rMusic_Init,
    .tp_alloc = PyType_GenericAlloc,
    .tp_new = PyType_GenericNew,
};

static PyObject* raudio_initialize(PyObject *self, PyObject *args) {
    if (IsAudioDeviceReady())
        Py_RETURN_NONE;
    InitAudioDevice();
    if (!IsAudioDeviceReady())
        PyErr_SetString(PyExc_SystemError, "Failed to initialize audio device");
    Py_RETURN_NONE;
}

static PyObject* raudio_shutdown(PyObject *self, PyObject *args) {
    if (IsAudioDeviceReady())
        CloseAudioDevice();
    Py_RETURN_NONE;
}

static PyObject* raudio_is_ready(PyObject *self, PyObject *args) {
    if (IsAudioDeviceReady()) {
        Py_RETURN_TRUE;
    } else {
        Py_RETURN_FALSE;
    }
}

static PyObject* raudio_set_master_volume(PyObject *self, PyObject *args) {
    float volume;
    if (!PyArg_ParseTuple(args, "f", &volume))
        return NULL;
    if (volume < 0.0f || volume > 1.0f) {
        PyErr_SetString(PyExc_ValueError, "Volume must be between 0.0 and 1.0");
        return NULL;
    }
    SetMasterVolume(volume);
    Py_RETURN_NONE;
}

static PyObject* raudio_get_master_volume(PyObject *self, PyObject *args) {
    return PyFloat_FromDouble((double)GetMasterVolume());
}

static PyMethodDef raudio_methods[] = {
    {"initialize", raudio_initialize, METH_VARARGS, "Initialize audio device"},
    {"shutdown", raudio_shutdown, METH_VARARGS, "Shutdown audio device"},
    {"is_ready", raudio_is_ready, METH_VARARGS, "Is audio device ready?"},
    {"set_master_volume", raudio_set_master_volume, METH_VARARGS, "Set master volume"},
    {"get_master_volume", raudio_get_master_volume, METH_VARARGS, "Get master volume"},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef raudio_module = {
    PyModuleDef_HEAD_INIT,
    "raudio",
    "Raylib raudio bindings for Python",
    -1,
    raudio_methods,
    NULL
};

#define STRUCTS \
    X(Wave) \
    X(AudioStream) \
    X(Sound) \
    X(Music)

PyMODINIT_FUNC PyInit_raudio(void) {
#define X(NAME) \
    if (PyType_Ready(&r##NAME##Type) < 0) \
        return NULL;
    STRUCTS
#undef X
    PyObject *m = PyModule_Create(&raudio_module);
    if (!m)
        return NULL;
#define X(NAME) \
    Py_INCREF(&r##NAME##Type); \
    if (PyModule_AddObject(m, #NAME, (PyObject*)&r##NAME##Type) < 0) { \
    Py_DECREF(&r##NAME##Type); \
        Py_DECREF(m); \
        return NULL; \
    }
    STRUCTS
#undef X
    return m;
}
