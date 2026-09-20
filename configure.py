# vim: set sts=2 ts=8 sw=2 tw=99 et:
import sys
from ambuild2 import run

parser = run.BuildParser(sourcePath=sys.path[0], api="2.2")
parser.options.add_argument(
    "--hl2sdk-root",
    type=str,
    dest="hl2sdk_root",
    default=None,
    help="Root search folder for HL2SDKs",
)
parser.options.add_argument(
    "--hl2sdk-manifests",
    type=str,
    dest="hl2sdk_manifests",
    default=None,
    help="HL2SDK manifests source tree folder (default: <mms_path>/hl2sdk-manifests)",
)
parser.options.add_argument(
    "--mms_path",
    type=str,
    dest="mms_path",
    default=None,
    help="Metamod:Source source tree folder (1.12 or 2.0)",
)
parser.options.add_argument(
    "--enable-debug",
    action="store_const",
    const="1",
    dest="debug",
    help="Enable debugging symbols",
)
parser.options.add_argument(
    "--enable-optimize",
    action="store_const",
    const="1",
    dest="opt",
    help="Enable optimization",
)
parser.options.add_argument(
    "--targets",
    type=str,
    dest="targets",
    default="x86",
    help="Target architecture (CS:GO servers are x86)",
)
parser.Configure()
