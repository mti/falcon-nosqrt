{
  lib,
  python3Packages,
  fetchFromGitHub,
  ...
}:

python3Packages.buildPythonPackage rec {
  pname = "chipwhisperer";
  version = "6.0.0";
  pyproject = true;

  src = fetchFromGitHub {
    owner = "newaetech";
    repo = "chipwhisperer";
    rev = "v${version}";
    hash = "sha256-/vqxsYTWWGUmJFB3e2QOYAOmNJVXGeur06FdOwqBfQo=";
    fetchSubmodules = true;
  };

  propagatedBuildInputs = with python3Packages; [
    setuptools
    setuptools-scm
  ];

  dependencies = with python3Packages; [
    bokeh
    configobj
    cycler
    cython
    datashader
    ecpy
    fastdtw
    holoviews
    ipywidgets
    jupyter
    jupyter-client
    libusb1
    matplotlib
    nbconvert
    #nbparameterise
    notebook
    numpy
    pandas
    #phoenixAES
    pycryptodome
    pyserial
    pyyaml
    terminaltables
    tqdm
  ];

  postPatch = ''
    substituteInPlace pyproject.toml \
      --replace "numpy<=1.26.4" "numpy"
  '';

  postInstall = ''
    cp -rv jupyter/ $out
    cp -rv firmware/ $out
    mkdir -p $out/etc/udev/rules.d
    cp -v 50-newae.rules $out/etc/udev/rules.d
  '';

  meta = {
    description = "ChipWhisperer Side-Channel Analysis Tool";
    homepage = "https://www.chipwhisperer.com";
    changelog = "https://github.com/${src.owner}/${src.repo}/blob/${src.rev}/CHANGES.md";
    license = lib.licenses.asl20;
  };
}
