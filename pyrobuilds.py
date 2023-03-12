from os import chdir, remove as rm
from os.path import isfile
from shutil import move as mv, copy as cp
from subprocess import run, check_output
from random import sample
from argparse import ArgumentParser
import bloatometer

def call_cmd(cmd, cwd='.'):
    return run(cmd, capture_output=True, shell=True, cwd=cwd)


def time_cmd(output=None):
    cmd =  ["/usr/bin/time", "-p", "-q", "-f", "%e"]
    if output is not None:
        cmd.extend(["-o", output])
    return cmd


class Repo:

    def __init__(self, directory):
        self._wd = directory

    def _callw(self, cmd):
        return call_cmd(cmd, cwd=self._wd)

    def init(self):
        cmd = f"git init {self._wd}"
        return self._callw(cmd)

    def add_all(self):
        cmd = "git add -fA"
        return self._callw(cmd)

    def commit(self, msg):
        cmd = f"git commit -m \"{msg}\""
        return self._callw(cmd)

    def branches(self):
        cmd = "git --no-pager branch -a"
        ret = self._callw(cmd)
        raw = ret.stdout.decode().split()
        return [b for b in raw if b not in {'', '*'}]

    def is_branch(self, name):
        return name in self.branches()

    def create_branch(self, name):
        cmd = f"git checkout -b {name}"
        return self._callw(cmd)

    def checkout(self, name):
        cmd = f"git checkout '{name}'"
        return self._callw(cmd)

    def config(self, prefix, field, val):
        cmd = f"git config {prefix}.{field} \"{val}\""
        return self._callw(cmd)

    def autoconfig(self):
        self.config("user", "email", "x")
        self.config("user", "name", "x")


class Kconfig:

    def __init__(self, csv):
        self._ktypes = set()
        self._ksubsys = set()
        self._kall = self._read_all_options_csv(csv)

    @staticmethod
    def xtsub(path):
        return path.split('/', 1)[0]

    def _read_all_options_csv(self, csv_file):
        kopts = dict()
        with open(csv_file, 'r') as stream:
            stream.readline()
            for line in stream:
                kop, ktyp, kfil = line.rstrip().split(',')
                kopts[kop] = {"type": ktyp, "file": kfil}
                self._ktypes.add(ktyp)
                self._ksubsys.add(self.xtsub(kfil))
        return kopts

    def type_of(self, kopt):
        return self._kall[kopt]["type"]

    def file_of(self, kopt):
        return self._kall[kopt]["file"]

    def subsystem_of(self, kopt):
        return self.xtsub(self.file_of(kopt))

    def koptions_of_type(self, *kt):
        for t in kt:
            assert t in self._ktypes, f"Type '{t}' is not a Kconfig type"
        return [*filter(lambda x: self.type_of(x) in kt, self._kall)]

    def koptions_of_subsystem(self, *ks):
        for s in ks:
            assert s in self._ksubsys,\
                f"Subsystem '{s}' is not a Linux subsystem"
        return [*filter(lambda x: self.subsystem_of(x) in ks, self._kall)]


class Kbuild:

    def __init__(self, source):
        self._src = source
        self._preset =  [
            "CONFIG_64BIT=y",
            "CONFIG_X86_64=y",
            "CONFIG_HAVE_GCC_PLUGINS=n",
            "CONFIG_GCC_PLUGINS=n",
            "CONFIG_GCC_PLUGIN_CYC_COMPLEXITY=n"
        ]
        self.TIME_FILE = ".time.pyro"
        self.STDOUT = ".stdout.pyro"
        self.STDERR = ".stderr.pyro"
        self.STATUS = ".status.pyro"


    def _write_preset(self, output):
        with open(output, 'w') as stream:
            stream.write("\n".join(self._preset))

    def oldconfig(self):
        cmd = "make oldconfig"
        return call_cmd(cmd, cwd=self._src)

    def randconfig(self):
        preset = "config.base"
        self._write_preset(preset)
        cmd = f"KCONFIG_ALLCONFIG={preset} make randconfig"
        ret = call_cmd(cmd, cwd=self._src)
        rm(preset)
        return ret

    def defconfig(self):
        cmd = "make defconfig"
        return call_cmd(cmd, cwd=self._src)


    def build(self, jobs=None, config=None, with_time=True, ccache=False):
        cmd = []

        if with_time:
            cmd.extend(time_cmd(output=self.TIME_FILE))

        if jobs is None:
            jobs = int(check_output("nproc"))+1

        if config is not None:
            if not isfile(config):
                raise FileNotFoundError(f"No such configuration {config}")
            if isfile(".config"):
                mv(".config", ".config.old")
            cp(config, ".config")

        cmd.append("make")
        if ccache:
            cmd.append('CC="ccache gcc"')
        cmd.append(f"-j{jobs}")

        cmd_s = " ".join(cmd)
        ret = call_cmd(cmd_s)
        with open(self.STATUS, 'w') as status:
            status.write(str(ret.returncode))
        if ret.stdout:
            with open(self.STDOUT, 'wb') as out:
                out.write(ret.stdout)
        if ret.stderr:
            with open(self.STDERR, 'wb') as err:
                err.write(ret.stderr)
        return ret.returncode

    def last_build_time(self):
        if not isfile(self.TIME_FILE):
            return -1
        with open(self.TIME_FILE) as stream:
            lines = stream.readlines()
        return float(lines[-1])

    def last_build_was_success(self):
        error_regex_list = ["(^.*): fatal error:(.*$)",
                            "(^.*):\s*error:\s*(.*$)",
                            "(^.*): (undefined reference.*$)",
                            "(^.*): (relocation truncated.*)",
                            "error: (.*undefined.*$)",
                            "^.*:\s*error in (.*);\s*(.*$)"
                            ]
        error_regex_str = "|".join(map(lambda x: f"({x})", error_regex_list))
        cmd = f"egrep '{error_regex_str}' {self.STDERR}"
        return isfile("vmlinux") \
            and not call_cmd(cmd, cwd=self._src).returncode == 0

    def built_files(self):
        n = 0
        with open(self.STDOUT, 'r') as stream:
            for line in stream:
                if isfile(line.split()[-1]):
                    n += 1
        return n


    def safe(self, vmlinux):
        grow, shrink, add, remove, up, down, delta, old, new, otot, ntot =\
            bloatometer.calc("vmlinux", vmlinux, "tTdDbBrR")
        return all(map(lambda x: x == 0, [grow, shrink, add, remove, up, down]))


class Config:

    def __init__(self, config):
        self._config = self.readconfig(config)

    @staticmethod
    def readconfig(config_file):
        d = {}
        with open(config_file, 'r') as stream:
            for line in stream:
                line = line.rstrip()
                if line.startswith("CONFIG_"):
                    name, val = line[7:].split("=", 1)
                    d[name] = val
                # if "is not set" in line:
                #     d[line[9:-11]] = "n"
        return d

    def value_of(self, option):
        if option in self._config:
            return self._config[option]
        return 'n'

    def diff(self, other):
        res = {"+": dict(), "-": dict(), "->": dict()}

        # a = readconfig(config1)
        # b = readconfig(config2)
        a, b = self._config, other._config

        for config in a:
            if config not in b:
                res['-'][config] = a[config]
            else:
                if a[config] != b[config]:
                    res['->'][config] = f"{a[config]} -> {b[config]}"
        for config in b:
            if config not in a:
                res['+'][config] = b[config]

        return res


class Mutator:

    def __init__(self, config, kconfig, wd="."):
        self._config = config
        self._kconfig = kconfig
        self._wd = wd
        self._history = []
        self._candidates = self._kconfig.\
            koptions_of_type("bool", "tristate")
        self._build_cfconfig()

    def _build_cfconfig(self):
        cmd = "make build_cfconfig"
        call_cmd(cmd, cwd=self._wd)

    def _mutate(self, option, value):
        cmd = ["export", "SRCARCH=x86", "LD=$(which ld)", "CC=$(which gcc)",
               "srctree=$(pwd)", ";"]
        cmd.extend(["scripts/kconfig/cfconfig", "Kconfig", option, value])
        cmd_s = " ".join(cmd)
        return call_cmd(cmd_s)

    def _try_random(self):
        while (sym := sample(self._candidates, 1).pop()) in self._history:
            pass
        vs = ['y', 'm', 'n']
        vs.remove(self._config.value_of(sym))
        val = sample(vs, 1).pop()
        return sym, val, self._mutate(sym, val)

    def random(self):
        sym, val, ret = self._try_random()
        while ret.returncode != 0:
            sym, val, ret = self._try_random()
        call_cmd("make oldconfig", cwd=self._wd)
        return sym, val, self._kconfig.subsystem_of(sym)


def debug(msg, end="\n"):
    print(msg, end=end, flush=True)


def main():

    parser = ArgumentParser(description="PyroBuildS")
    parser.add_argument("--src",
                        type=str,
                        required=True,
                        help="Source code")
    parser.add_argument("--base",
                        type=str,
                        required=True,
                        choices=["defconfig", "randconfig"],
                        help="Initial config: defconfig or randconfig")
    parser.add_argument("--strategy",
                        type=str,
                        required=True,
                        choices=["star", "explorer"],
                        help="Source code")
    parser.add_argument("--check",
                        action="store_true",
                        default=False,
                        help="Compare with clean build")
    parser.add_argument("-n",
                        required=True,
                        type=int,
                        help="Number of iteration")
    parser.add_argument("--graph",
                        action="store_true",
                        default=False)

    args = parser.parse_args()
    src = args.src.rstrip('/')
    base = args.base
    strat = args.strategy
    budget = args.n
    to_check = args.check
    graph = args.graph
    # ----------------------------------------------------------------------

    # Kconfig
    kconfig = Kconfig("x86_options.csv")

    chdir(src)

    # Initialize git
    repo = Repo(".")
    repo.init()
    repo.autoconfig()
    repo.add_all()
    repo.commit("source")
    brbase = "base"
    repo.create_branch(brbase)

    # Kbuild
    kbuild = Kbuild(".")
    if base == "defconfig":
        kbuild.defconfig()
    elif base == "randconfig":
        kbuild.randconfig()

    # Base config
    base_config = Config(".config")
    # Mutator
    mutator = Mutator(base_config, kconfig)

    if graph:
        graphviz = ["digraph pyrobuilds {"]
    # Algorithm
    kbuild.build()
    btime, bstatus = kbuild.last_build_time(), kbuild.last_build_was_success()
    bbtime = btime
    nfiles = kbuild.built_files()
    debug(f"{brbase},{btime},{bstatus},{nfiles},,,")
    if graph:
        for_graphviz = ""
        if bstatus:
            for_graphviz += "Build successful"
        else:
            for_graphviz += "Build failed"
        graphviz.append(f'{brbase}[label="{base}\\n{for_graphviz}\\nBuild time: {btime}\\nFiles: {nfiles}"]')
    repo.add_all()
    repo.commit(f"Clean build of {brbase}")
    bpref = strat
    if graph:
        prev = brbase
        with open("../out.dot", 'w') as dot:
            dot.write("\n".join(graphviz) + "}")
    for i in range(1, budget+1):
        brn = f"{bpref}{i:02d}"
        repo.create_branch(brn)
        sym, val, sub = mutator.random()
        mutant = Config(".config")
        kbuild.build()
        btime = kbuild.last_build_time()
        bstatus = kbuild.last_build_was_success()
        nfiles = kbuild.built_files()
        diff = base_config.diff(mutant)
        ndiff = sum(map(len, diff.values()))
        debug(f"{brn},{btime},{bstatus},{nfiles},{sym}={val},{sub},{ndiff}", end="")
        repo.add_all()
        repo.commit(f"Build for {brn}")
        if to_check:
            if bstatus:
                cp("vmlinux", "/tmp/vmlinux")
            cp(".config", "/tmp/.config")
            repo.checkout("master")
            brn_clean = brn + "c"
            repo.create_branch(brn_clean)
            cp("/tmp/.config", ".config")
            kbuild.build()
            btime_c = kbuild.last_build_time()
            bstatus_c = kbuild.last_build_was_success()
            consistency = bstatus == bstatus_c and kbuild.safe("/tmp/vmlinux")
            debug(f",{btime_c},{bstatus_c},{consistency}")
            if isfile("/tmp/vmlinux"):
                rm("/tmp/vmlinux")
            rm("/tmp/.config")
            repo.checkout(brn)
        else:
            debug("")

        if graph:
            for_graphviz = f'{brn}[label="'
            if bstatus:
                for_graphviz += "Build successful"
            else:
                for_graphviz += "Build failed"
            for_graphviz += '\\n'
            for_graphviz += f"Build time: {btime}s"
            if to_check:
                for_graphviz += f"/{btime_c}s"
            for_graphviz += '\\n'
            for_graphviz += f"Mutation: {sym}={val} ({sub})\\n"
            for_graphviz += f"Files: {nfiles}\\n"
            for_graphviz +=\
                f"Diff: +{len(diff['+'])},-{len(diff['-'])},->{len(diff['->'])}\\n"
            if to_check:
                for_graphviz += "Consistency: "
                if consistency:
                    for_graphviz += "Yes"
                else:
                    for_graphviz += "No"
            for_graphviz += '"'
            if to_check:
                if btime < btime_c - 5:
                    for_graphviz += ', color=green'
                else:
                    for_graphviz += ', color=red'
            else:
                if btime < bbtime - 5:
                    for_graphviz += ', color=green'
                else:
                    for_graphviz += ', color=red'
            for_graphviz += ']'
        graphviz.append(for_graphviz)
        if strat == "star":
            repo.checkout(brbase)
            if graph:
                graphviz.append(f"{brbase} -> {brn}")
        else:
            if graph:
                graphviz.append(f"{prev} -> {brn}")
            prev = brn
        if graph:
            with open("../out.dot", 'w') as dot:
                dot.write("\n".join(graphviz) + "}")



if __name__ == "__main__":
    main()
