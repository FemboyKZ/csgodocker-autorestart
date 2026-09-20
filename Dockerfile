FROM registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest

RUN git clone https://github.com/alliedmodders/ambuild.git \
	&& cd ambuild && python3 setup.py install

WORKDIR /work

# Builds one package per Metamod:Source flavor (submodules must be checked out):
#   metamod-source     -> output/mm-1.12 (SourceHook)
#   metamod-source-2.0 -> output/mm-2.0  (KHook)
CMD ["bash", "-ec", "\
    for pair in metamod-source:mm-1.12 metamod-source-2.0:mm-2.0; do \
      mms=${pair%%:*}; flavor=${pair##*:}; \
      rm -rf build/$flavor output/$flavor && mkdir -p build/$flavor output && \
      (cd build/$flavor && python3 ../../configure.py --mms_path=/work/$mms --hl2sdk-root=/work --enable-optimize && ambuild) && \
      cp -r build/$flavor/package output/$flavor; \
    done"]
