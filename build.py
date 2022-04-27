#!/usr/bin/env python3.9

# - build and test varnish-cache and a number of vmods from a JSON
#   config file
# - generate a JSON output file with the git commit hash of each
#   of the used repositories
# - leave no trace: The build happens entirely out-of-tree in
#   temporary directories
#
# ARGS: [check]

import json
import os
import sys

from contextlib import contextmanager
from multiprocessing import cpu_count
from subprocess import run
from tempfile import TemporaryDirectory


def r(*args, **kwargs):
    r = run(*args, **kwargs)
    if r.returncode:
        print("FAILED " + " ".join(*args))
        sys.exit(r.returncode)
    return r


@contextmanager
def pushd(new_dir):
    previous_dir = os.getcwd()
    os.chdir(new_dir)
    try:
        yield
    finally:
        os.chdir(previous_dir)


def bootstrap_autogen_configure(srcdir, prefix, **kwargs):
    """Run a dumb check if bootstrap looks like it would call configure
    Dridi-style.
    * If bootstrap calls configure, return the call to be made from the
      builddir.
    * Otherwise, call bootstrap or autogen and return the configure
      call to be made from builddir
    """

    p = "--prefix=" + prefix
    a = os.path.join(srcdir, "bootstrap")

    if os.path.isfile(a):
        with open(a, "r") as file:
            if "/configure" in file.read():
                return [a, p]
    else:
        a = os.path.join(srcdir, "autogen.sh")

    with pushd(srcdir):
        r([a], **kwargs)

    a = os.path.join(srcdir, "configure")
    return [a, p]


def make(configure, **kwargs):
    r(configure, **kwargs)
    r(["make", "-j", str(cpu_count() + 2)], **kwargs)
    if len(sys.argv) >= 2 and sys.argv[1] == "check":
        r(["make", "-j", str(cpu_count() * 15), "check"], **kwargs)
    r(["make", "install"], **kwargs)


def build(srcdir, prefix, **kwargs):
    configure = bootstrap_autogen_configure(srcdir, prefix, **kwargs)
    run(["make", "maintainer-clean"])
    run(["make", "distclean"])
    with TemporaryDirectory() as builddir:
        with pushd(str(builddir)):
            make(configure, **kwargs)


def build_vmod(name, prefix):
    env = os.environ
    # only required by vmod_dispatch
    env["VARNISHSRC"] = varnishsrc
    env["PKG_CONFIG_PATH"] = os.path.join(prefix, "lib", "pkgconfig")
    env["ACLOCAL_PATH"] = os.path.join(prefix, "share", "aclocal")
    build(os.getcwd(), prefix, env=env)


# XXX how can we avoid special casing?
# XXX does not support out-of-tree build
def build_maxmind(prefix):
    with pushd("libmaxminddb"):
        configure = bootstrap_autogen_configure(os.getcwd(), prefix)
        make(configure)


def clone_build_vmod(vmod, prefix):
    with TemporaryDirectory() as gitdir:
        with pushd(str(gitdir)):
            r(["git", "clone", "--recursive", vmod["git"]])
            with pushd(vmod["vmod"]):
                r(["git", "checkout", vmod["branch"]])
                sub = run(["git", "describe"], capture_output=True, text=True)
                if sub.returncode:
                    sub = r(["git", "rev-parse", "--short", "HEAD"],
                            capture_output=True, text=True)
                vmod["rev"] = sub.stdout.rstrip()
                del vmod["branch"]
                if "geoip2" in vmod["vmod"]:
                    build_maxmind(prefix)
                build_vmod(vmod["vmod"], prefix)


def build_vmods(vmods, prefix):
    for vmod in vmods:
        clone_build_vmod(vmod, prefix)


varnishsrc = os.getcwd()
vmods = None
with open(os.path.join(varnishsrc, "VMODS.json"), "rb") as file:
    vmods = json.loads(file.read())

with TemporaryDirectory() as prefix:
    build(varnishsrc, str(prefix))
    build_vmods(vmods, str(prefix))

with open(os.path.join(varnishsrc, "VMODS_BUILT.json"), "w") as file:
    json.dump(vmods, file, indent=4, sort_keys=True)
