#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# Copyright © 2018 Thibault Saunier <tsaunier@igalia.com>
#
# This library is free software; you can redistribute it and/or modify it under
# the terms of the GNU Lesser General Public License as published by the Free
# Software Foundation; either version 2.1 of the License, or (at your option)
# any later version.
#
# This library is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
# details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this library.  If not, see <http://www.gnu.org/licenses/>.

import argparse
import json
import os
import sys
import re
import subprocess
import tempfile
from pathlib import Path as P
from argparse import ArgumentParser

from collections import OrderedDict
try:
    from collections.abc import Mapping
except ImportError:  # python <3.3
    from collections import Mapping


class GstPluginsHotdocConfGen:
    def __init__(self):

        parser = ArgumentParser()
        parser.add_argument('--builddir', type=P)
        parser.add_argument('--gst_cache_file', type=P)
        parser.add_argument('--sitemap', type=P)
        parser.add_argument('--index', type=P)
        parser.add_argument('--c_flags')
        parser.add_argument('--gst_index', type=P)
        parser.add_argument('--gst_c_sources', nargs='*', default=[])
        parser.add_argument('--project_version')
        parser.add_argument('--include_paths', nargs='*', default=[])
        parser.add_argument('--gst_c_source_filters', nargs='*', default=[])
        parser.add_argument('--gst_c_source_file', type=P)

        parser.parse_args(namespace=self, args=sys.argv[2:])

    def generate_plugins_configs(self):
        plugin_files = []

        if self.gst_c_source_file is not None:
            with self.gst_c_source_file.open() as fd:
                gst_c_source_map = json.load(fd)
        else:
            gst_c_source_map = {}

        with self.gst_cache_file.open() as fd:
            all_plugins = json.load(fd)

            for plugin_name in all_plugins.keys():
                conf = self.builddir / f'plugin-{plugin_name}.json'
                plugin_files.append(str(conf))

                # New-style, sources are explicitly provided, as opposed to using wildcards
                if plugin_name in gst_c_source_map:
                    gst_c_sources = gst_c_source_map[plugin_name].split(os.pathsep)
                else:
                    gst_c_sources = self.gst_c_sources

                with conf.open('w') as f:
                    json.dump({
                        'sitemap': str(self.sitemap),
                        'index': str(self.index),
                        'gst_index': str(self.index),
                        'output': f'plugin-{plugin_name}',
                        'conf': str(conf),
                        'project_name': plugin_name,
                        'project_version': self.project_version,
                        'gst_cache_file': str(self.gst_cache_file),
                        'gst_plugin_name': plugin_name,
                        'c_flags': self.c_flags,
                        'gst_smart_index': True,
                        'gst_c_sources': gst_c_sources,
                        'gst_c_source_filters': [str(s) for s in self.gst_c_source_filters],
                        'include_paths': self.include_paths,
                        'gst_order_generated_subpages': True,
                    }, f, indent=4)

        return plugin_files


# Marks values in the json file as "unstable" so that they are
# not updated automatically, this aims at making the cache file
# stable and handle corner cases were we can't automatically
# make it happen. For properties, the best way is to use th
# GST_PARAM_DOC_SHOW_DEFAULT flag.
UNSTABLE_VALUE = "unstable-values"



def dict_recursive_update(d, u):
    modified = False
    unstable_values = d.get(UNSTABLE_VALUE, [])
    if not isinstance(unstable_values, list):
        unstable_values = [unstable_values]
    for k, v in u.items():
        if isinstance(v, Mapping):
            r = d.get(k, {})
            modified |= dict_recursive_update(r, v)
            d[k] = r
        elif k not in unstable_values:
            modified = True
            if k == "package":
                d[k] = re.sub(" git$| source release$| prerelease$", "", v)
            else:
                d[k] = u[k]
    return modified


def test_unstable_values():
    current_cache = { "v1": "yes", "unstable-values": "v1"}
    new_cache = { "v1": "no" }

    assert(dict_recursive_update(current_cache, new_cache) == False)

    new_cache = { "v1": "no", "unstable-values": "v2" }
    assert(dict_recursive_update(current_cache, new_cache) == True)

    current_cache = { "v1": "yes", "v2": "yay", "unstable-values": "v1",}
    new_cache = { "v1": "no" }
    assert(dict_recursive_update(current_cache, new_cache) == False)

    current_cache = { "v1": "yes", "v2": "yay", "unstable-values": "v2"}
    new_cache = { "v1": "no", "v2": "unstable" }
    assert (dict_recursive_update(current_cache, new_cache) == True)
    assert (current_cache == { "v1": "no", "v2": "yay", "unstable-values": "v2" })

if __name__ == "__main__":
    if sys.argv[1] == "hotdoc-config":
        fs = GstPluginsHotdocConfGen().generate_plugins_configs()
        print(os.pathsep.join(fs))
        sys.exit(0)

    cache_filename = sys.argv[1]
    output_filename = sys.argv[2]
    build_root = os.environ.get('MESON_BUILD_ROOT', '')

    subenv = os.environ.copy()
    cache = {}
    try:
        with open(cache_filename, newline='\n', encoding='utf8') as f:
            cache = json.load(f)
    except FileNotFoundError:
        pass

    out = output_filename + '.tmp'
    cmd = [os.path.join(os.path.dirname(os.path.realpath(__file__)), 'gst-hotdoc-plugins-scanner'), out]
    gst_plugins_paths = []
    for plugin_path in sys.argv[3:]:
        cmd.append(plugin_path)
        gst_plugins_paths.append(os.path.dirname(plugin_path))

    try:
        with open(os.path.join(build_root, 'GstPluginsPath.json'), newline='\n', encoding='utf8') as f:
            plugin_paths = os.pathsep.join(json.load(f))
    except FileNotFoundError:
        plugin_paths = ""

    if plugin_paths:
        subenv['GST_PLUGIN_PATH'] = subenv.get('GST_PLUGIN_PATH', '') + ':' + plugin_paths

    # Hide stderr unless an actual error happens as we have cases where we get g_warnings
    # and other issues because plugins are being built while `gst_init` is called
    stderrlogfile = output_filename + '.stderr'
    with open(stderrlogfile, 'w', encoding='utf8') as log:
        try:
            data = subprocess.check_output(cmd, env=subenv, stderr=log, encoding='utf8', universal_newlines=True)
        except subprocess.CalledProcessError as e:
            log.flush()
            with open(stderrlogfile, 'r', encoding='utf8') as f:
                print(f.read(), file=sys.stderr, end='')
            raise

    with open(out, 'r', newline='\n', encoding='utf8') as jfile:
        try:
            plugins = json.load(jfile, object_pairs_hook=OrderedDict)
        except json.decoder.JSONDecodeError:
            print("Could not decode:\n%s" % jfile.read(), file=sys.stderr)
            raise

    modified = dict_recursive_update(cache, plugins)

    with open(output_filename, 'w', newline='\n', encoding='utf8') as f:
        json.dump(cache, f, indent=4, sort_keys=True, ensure_ascii=False)

    if modified:
        with open(cache_filename, 'w', newline='\n', encoding='utf8') as f:
            json.dump(cache, f, indent=4, sort_keys=True, ensure_ascii=False)
