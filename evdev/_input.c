#include "py/nlr.h"
#include "py/obj.h"
#include "py/qstr.h"
#include "py/runtime.h"
#include "py/binary.h"
#include <stdio.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef __FreeBSD__
#include <dev/evdev/input.h>
#else
#include <linux/input.h>
#endif

#define MAX_NAME_SIZE 256

extern char*  EV_NAME[EV_CNT];
extern int    EV_TYPE_MAX[EV_CNT];
extern char** EV_TYPE_NAME[EV_CNT];
extern char*  BUS_NAME[];

int test_bit(const char* bitmask, int bit) {
    return bitmask[bit/8] & (1 << (bit % 8));
}

STATIC mp_obj_t ioctl_devinfo(mp_obj_t arg) {
    mp_obj_t rvalue = mp_const_none;
    int fd = mp_obj_get_int(arg);
    struct input_id iid;
    char name[MAX_NAME_SIZE];
    char phys[MAX_NAME_SIZE] = {0};
    char uniq[MAX_NAME_SIZE] = {0};

    //printf("%s\n", __FUNCTION__);
    memset(&iid,  0, sizeof(iid));

    if (ioctl(fd, EVIOCGID, &iid) < 0)                 goto on_err;
    if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0) goto on_err;

    // Some devices do not have a physical topology associated with them
    ioctl(fd, EVIOCGPHYS(sizeof(phys)), phys);

    // Some kernels have started reporting bluetooth controller MACs as phys.
    // This lets us get the real physical address. As with phys, it may be blank.
    ioctl(fd, EVIOCGUNIQ(sizeof(uniq)), uniq);

    mp_obj_t objs[7] = {0};
    objs[0] = mp_obj_new_int(iid.bustype);
    objs[1] = mp_obj_new_int(iid.vendor);
    objs[2] = mp_obj_new_int(iid.product);
    objs[3] = mp_obj_new_int(iid.version);
    objs[4] = mp_obj_new_str(name, strlen(name));
    objs[5] = mp_obj_new_str(phys, strlen(phys));
    objs[6] = mp_obj_new_str(uniq, strlen(uniq));

    rvalue = mp_obj_new_tuple(7, objs);
on_err:
    return rvalue;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(ioctl_devinfo_obj, ioctl_devinfo);

STATIC mp_obj_t ioctl_EVIOCGVERSION(mp_obj_t arg) {
    mp_obj_t rvalue = mp_const_none;
    int fd = mp_obj_get_int(arg);
    int res = 0;

    //printf("%s\n", __FUNCTION__);
    if (-1 == ioctl(fd, EVIOCGVERSION, &res)) goto on_err;
    rvalue = mp_obj_new_int(res);
on_err:
    return rvalue;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(ioctl_EVIOCGVERSION_obj, ioctl_EVIOCGVERSION);

STATIC mp_obj_t ioctl_capabilities(mp_obj_t arg) {
    mp_obj_t rvalue = mp_const_none;
    int fd = mp_obj_get_int(arg);
    int ev_type, ev_code;
    char ev_bits[EV_MAX/8 + 1], code_bits[KEY_MAX/8 + 1];
    struct input_absinfo absinfo;

    // @todo: figure out why fd gets zeroed on an ioctl after the
    // refactoring and get rid of this workaround
    const int _fd = fd;

    //printf("%s\n", __FUNCTION__);
    // Capabilities is a mapping of supported event types to lists of handled
    // events e.g: {1: [272, 273, 274, 275], 2: [0, 1, 6, 8]}
    mp_obj_t capabilities = mp_obj_new_dict(0);
    mp_obj_t eventcodes = mp_const_none;
    mp_obj_t evlong = mp_const_none;
    mp_obj_t capability = mp_const_none;
    mp_obj_t py_absinfo = mp_const_none;
    mp_obj_t absitem = mp_const_none;

    memset(&ev_bits, 0, sizeof(ev_bits));

    if (ioctl(_fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) < 0)
        goto on_err;

    // Build a dictionary of the device's capabilities
    for (ev_type=0 ; ev_type<EV_MAX ; ev_type++) {
        if (test_bit(ev_bits, ev_type)) {

            eventcodes = mp_obj_new_list(0, NULL);
            capability = mp_obj_new_int_from_ll(ev_type);

            memset(&code_bits, 0, sizeof(code_bits));
            ioctl(_fd, EVIOCGBIT(ev_type, sizeof(code_bits)), code_bits);

            for (ev_code = 0; ev_code < KEY_MAX; ev_code++) {
                if (test_bit(code_bits, ev_code)) {
                    // Get abs{min,max,fuzz,flat} values for ABS_* event codes
                    if (ev_type == EV_ABS) {
                        memset(&absinfo, 0, sizeof(absinfo));
                        ioctl(_fd, EVIOCGABS(ev_code), &absinfo);

                        mp_obj_t objs[6] = {0};
                        objs[0] = mp_obj_new_int(absinfo.value);
                        objs[1] = mp_obj_new_int(absinfo.minimum);
                        objs[2] = mp_obj_new_int(absinfo.maximum);
                        objs[3] = mp_obj_new_int(absinfo.fuzz);
                        objs[4] = mp_obj_new_int(absinfo.flat);
                        objs[5] = mp_obj_new_int(absinfo.resolution);
                        py_absinfo = mp_obj_new_tuple(6, objs);

                        evlong = mp_obj_new_int_from_ll(ev_code);
                        objs[0] = evlong;
                        objs[1] = py_absinfo;
                        absitem = mp_obj_new_tuple(2, objs);

                        // absitem -> tuple(ABS_X, (0, 255, 0, 0))
                        mp_obj_list_append(eventcodes, absitem);

                    }
                    else {
                        evlong = mp_obj_new_int_from_ll(ev_code);
                        mp_obj_list_append(eventcodes, evlong);
                    }
                }
            }
            // capabilities[EV_KEY] = [KEY_A, KEY_B, KEY_C, ...]
            // capabilities[EV_ABS] = [(ABS_X, (0, 255, 0, 0)), ...]
            mp_obj_dict_store(capabilities, capability, eventcodes);
        }
    }

    rvalue = capabilities;

on_err:
    return rvalue;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(ioctl_capabilities_obj, ioctl_capabilities);

STATIC mp_obj_t ioctl_EVIOCGREP(mp_obj_t arg) {
    mp_obj_t rvalue = mp_const_none;
    int fd = mp_obj_get_int(arg);
    int res = 0;

    printf("%s-not implemented yet\n", __FUNCTION__);
    return rvalue;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(ioctl_EVIOCGREP_obj, ioctl_EVIOCGREP);

STATIC mp_obj_t ioctl_EVIOCSREP(mp_obj_t arg) {
    mp_obj_t rvalue = mp_const_none;
    int fd = mp_obj_get_int(arg);
    int res = 0;

    printf("%s-not implemented yet\n", __FUNCTION__);
    return rvalue;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(ioctl_EVIOCSREP_obj, ioctl_EVIOCSREP);

STATIC mp_obj_t ioctl_EVIOCGRAB(mp_obj_t arg) {
    mp_obj_t rvalue = mp_const_none;
    int fd = mp_obj_get_int(arg);
    int res = 0;

    printf("%s-not implemented yet\n", __FUNCTION__);
    return rvalue;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(ioctl_EVIOCGRAB_obj, ioctl_EVIOCGRAB);

STATIC mp_obj_t ioctl_EVIOCGEFFECTS(mp_obj_t arg) {
    mp_obj_t rvalue = mp_const_none;
    int fd = mp_obj_get_int(arg);
    int res = 0;

    //printf("%s\n", __FUNCTION__);
    if (-1 == ioctl(fd, EVIOCGEFFECTS, &res)) goto on_err;
    rvalue = mp_obj_new_int(res);
on_err:
    return rvalue;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(ioctl_EVIOCGEFFECTS_obj, ioctl_EVIOCGEFFECTS);

STATIC mp_obj_t ioctl_EVIOCG_bits(mp_obj_t arg) {
    mp_obj_t rvalue = mp_const_none;
    int fd = mp_obj_get_int(arg);
    int res = 0;

    printf("%s-not implemented yet\n", __FUNCTION__);
    return rvalue;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(ioctl_EVIOCG_bits_obj, ioctl_EVIOCG_bits);

STATIC mp_obj_t device_read(mp_obj_t arg) {
    mp_obj_t rvalue = mp_const_none;
    int fd = mp_obj_get_int(arg);
    int res = 0;

    printf("%s-not implemented yet\n", __FUNCTION__);
    return rvalue;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(device_read_obj, device_read);

STATIC mp_obj_t device_read_many(mp_obj_t arg) {
    mp_obj_t rvalue = mp_const_none;
    int fd = mp_obj_get_int(arg);
    int i = 0;

    struct input_event event[64];

    //printf("%s\n", __FUNCTION__);
    size_t event_size = sizeof(struct input_event);
    ssize_t nread = read(fd, event, event_size*64);

    if (nread < 0)
        goto on_err;

    rvalue = mp_obj_new_list(0, NULL);
    for (i = 0 ; i < nread/event_size ; i++) {
        mp_obj_t objs[6] = {0};
        objs[0] = mp_obj_new_int_from_ll(event[i].time.tv_sec);
        objs[1] = mp_obj_new_int_from_ll(event[i].time.tv_usec);
        objs[2] = mp_obj_new_int(event[i].type);
        objs[3] = mp_obj_new_int(event[i].code);
        objs[4] = mp_obj_new_int_from_ll(event[i].value);
        mp_obj_list_append(rvalue, mp_obj_new_tuple(5, objs));
    }
on_err:
    return rvalue;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(device_read_many_obj, device_read_many);

STATIC mp_obj_t upload_effect(mp_obj_t arg) {
    mp_obj_t rvalue = mp_const_none;
    int fd = mp_obj_get_int(arg);
    int res = 0;

    printf("%s-not implemented yet\n", __FUNCTION__);
    return rvalue;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(upload_effect_obj, upload_effect);

STATIC mp_obj_t erase_effect(mp_obj_t arg) {
    mp_obj_t rvalue = mp_const_none;
    int fd = mp_obj_get_int(arg);
    int res = 0;

    printf("%s-not implemented yet\n", __FUNCTION__);
    return rvalue;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(erase_effect_obj, erase_effect);

mp_obj_module_t *init__input(void) {
    printf("%s\n", __FUNCTION__);
    mp_obj_t m = mp_obj_new_module(qstr_from_str("_input"));
    mp_store_attr(m, qstr_from_str("ioctl_devinfo"), (mp_obj_t)&ioctl_devinfo_obj);
    mp_store_attr(m, qstr_from_str("ioctl_capabilities"), (mp_obj_t)&ioctl_capabilities_obj);
    mp_store_attr(m, qstr_from_str("ioctl_EVIOCGVERSION"), (mp_obj_t)&ioctl_EVIOCGVERSION_obj);
    mp_store_attr(m, qstr_from_str("ioctl_EVIOCGREP"), (mp_obj_t)&ioctl_EVIOCGREP_obj);
    mp_store_attr(m, qstr_from_str("ioctl_EVIOCSREP"), (mp_obj_t)&ioctl_EVIOCSREP_obj);
    mp_store_attr(m, qstr_from_str("ioctl_EVIOCGRAB"), (mp_obj_t)&ioctl_EVIOCGRAB_obj);
    mp_store_attr(m, qstr_from_str("ioctl_EVIOCGEFFECTS"), (mp_obj_t)&ioctl_EVIOCGEFFECTS_obj);
    mp_store_attr(m, qstr_from_str("ioctl_EVIOCG_bits"), (mp_obj_t)&ioctl_EVIOCG_bits_obj);
    mp_store_attr(m, qstr_from_str("device_read"), (mp_obj_t)&device_read_obj);
    mp_store_attr(m, qstr_from_str("device_read_many"), (mp_obj_t)&device_read_many_obj);
    mp_store_attr(m, qstr_from_str("upload_effect"), (mp_obj_t)&upload_effect_obj);
    mp_store_attr(m, qstr_from_str("erase_effect"), (mp_obj_t)&erase_effect_obj);
    mp_store_name(qstr_from_str("_input"), m);
    return m;
}
