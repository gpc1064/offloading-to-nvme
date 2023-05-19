from argparse import ArgumentParser
from examples.seismic import (Receiver, TimeAxis, RickerSource,
                              AcquisitionGeometry)
from examples.seismic.acoustic import AcousticWaveSolver
from examples.seismic.tti import AnisotropicWaveSolver
import numpy as np

from util import from_hdf5
from shutil import copyfile
from devito import TimeFunction
from devito.data.allocators import ExternalAllocator
from devito import configuration, compiler_registry
from devito.arch.compiler import GNUCompiler

files = {
    'compression-forward' : 'src/compression/non-mpi/forward.c',
    'compression-gradient' : 'src/compression/non-mpi/gradient.c',
    'forward' : 'src/non-mpi/forward.c',
    'gradient' : 'src/non-mpi/gradient.c',
    'forward-mpi' : 'src/mpi/forward.c',
    'gradient-mpi' : 'src/mpi/gradient.c',
    'ram-forward' : 'src/ram/non-mpi/forward.c',
    'ram-gradient' : 'src/ram/non-mpi/gradient.c',
    'ram-forward-mpi' : 'src/ram/mpi/forward.c',
    'ram-gradient-mpi' : 'src/ram/mpi/gradient.c',
    }

def operatorInjector(op, payload):

    configuration['jit-backdoor'] = True
    configuration.add('payload', payload)

    # Force compilation *and* loading upon the next `op.apply`

    op._lib = None
    op._cfunction = None

    if op._soname:
        del op._soname

    cfile = "%s.c" % str(op._compiler.get_jit_dir().joinpath(op._soname))

    copyfile(payload, cfile)

    return

def overthrust_setup(filename, kernel='OT2', tn=100, src_coordinates=None,
                     space_order=2, datakey='m0', nbpml=40, dtype=np.float32,
                     **kwargs):
    model = from_hdf5(filename, space_order=space_order, nbpml=nbpml,
                      datakey=datakey, dtype=dtype)

    shape = model.shape
    spacing = model.spacing
    nrec = shape[0]

    if src_coordinates is None:
        src_coordinates = np.empty((1, len(spacing)))
        src_coordinates[0, :] = np.array(model.domain_size) * .5
        if len(shape) > 1:
            src_coordinates[0, -1] = model.origin[-1] + 2 * spacing[-1]

    rec_coordinates = np.empty((nrec, len(spacing)))
    rec_coordinates[:, 0] = np.linspace(0., model.domain_size[0], num=nrec)
    if len(shape) > 1:
        rec_coordinates[:, 1] = np.array(model.domain_size)[1] * .5
        rec_coordinates[:, -1] = model.origin[-1] + 2 * spacing[-1]

    # Create solver object to provide relevant operator
    geometry = AcquisitionGeometry(model, rec_coordinates, src_coordinates,
                                   t0=0.0, tn=tn, src_type='Ricker', f0=0.008)

    solver = AcousticWaveSolver(model, geometry, kernel=kernel,
                                space_order=space_order, **kwargs)
    return solver


def overthrust_setup_tti(filename, tn=1000, space_order=2, nbpml=40,
                         **kwargs):
    model = from_hdf5(filename, space_order=space_order, nbpml=nbpml,
                      datakey='m0', dtype=np.float32)
    shape = model.vp.shape
    spacing = model.shape
    nrec = shape[0]

    # Derive timestepping from model spacing
    dt = model.critical_dt
    t0 = 0.0
    time_range = TimeAxis(start=t0, stop=tn, step=dt)

    # Define source geometry (center of domain, just below surface)
    src = RickerSource(name='src', grid=model.grid, f0=0.015,
                       time_range=time_range)
    src.coordinates.data[0, :] = np.array(model.domain_size) * .5
    if len(shape) > 1:
        src.coordinates.data[0, -1] = model.origin[-1] + 2 * spacing[-1]

    # Define receiver geometry (spread across x, just below surface)
    rec = Receiver(name='rec', grid=model.grid, time_range=time_range,
                   npoint=nrec)
    rec.coordinates.data[:, 0] = np.linspace(0., model.domain_size[0],
                                             num=nrec)
    if len(shape) > 1:
        rec.coordinates.data[:, 1:] = src.coordinates.data[0, 1:]

    # Create solver object to provide relevant operators
    return AnisotropicWaveSolver(model, source=src, receiver=rec,
                                 space_order=space_order, **kwargs)


def run(space_order=4, kernel='OT4', nbpml=40, filename='', to_disk=True, compression=False, mpi=False, **kwargs):

    if kernel in ['OT2', 'OT4']:
        solver = overthrust_setup(filename=filename, nbpml=nbpml,
                                  space_order=space_order, kernel=kernel,
                                  **kwargs)
    elif kernel == 'TTI':
        solver = overthrust_setup_tti(filename=filename, nbpml=nbpml,
                                      space_order=space_order, kernel=kernel,
                                      **kwargs)
    else:
        raise ValueError()

    grid = solver.model.grid

    rec = solver.geometry.rec

    dt = solver.model.critical_dt

    if to_disk:

        u = TimeFunction(name='u', grid=grid, time_order=2, space_order=space_order)
        fw_op = solver.op_fwd(save=False)
        rev_op = solver.op_grad(save=False)

        if compression:
            operatorInjector(fw_op, files ['compression-forward'])
            operatorInjector(rev_op, files ['compression-gradient'])

        elif mpi:
            operatorInjector(fw_op, files ['forward-mpi'])
            operatorInjector(rev_op, files ['gradient-mpi'])

        else:
            operatorInjector(fw_op, files ['forward'])
            operatorInjector(rev_op, files ['gradient'])

    else:

        u = TimeFunction(name='u', grid=grid, time_order=2, space_order=space_order, save=solver.geometry.nt)
        fw_op = solver.op_fwd(save=True)
        rev_op = solver.op_grad(save=True)

        if mpi:
            operatorInjector(fw_op, files ['ram-forward-mpi'])
            operatorInjector(rev_op, files ['ram-gradient-mpi'])
        else:
            operatorInjector(fw_op, files ['ram-forward'])
            operatorInjector(rev_op, files ['ram-gradient'])

    fw_op.apply(rec=rec, src=solver.geometry.src, u=u, dt=dt)
    rev_op.apply(u=u, dt=dt, rec=rec)

    return

if __name__ == "__main__":

    description = ("Example script for a set of acoustic operators.")

    parser = ArgumentParser(description=description)

    parser.add_argument("-so", "--space_order", default=6,
                        type=int, help="Space order of the simulation")

    parser.add_argument("--nbpml", default=40,
                        type=int, help="Number of PML layers around the domain")

    parser.add_argument("-k", dest="kernel", default='OT2',
                        choices=['OT2', 'OT4', 'TTI'],
                        help="Choice of finite-difference kernel")

    parser.add_argument("--mpi", default=False, action="store_true",
                        help="Use MPI on experiments")

    parser.add_argument("--compression", default=False, action="store_true",
                        help="Use Compression on experiments")

    parser.add_argument("--rate", default=16,
                        type=int, help="Set the Compression Rate to compression")

    parser.add_argument("--disks", default=8, type=int,
                        help="Number of PML layers around the domain")

    parser.add_argument("--ram", default=False, action="store_true",
                        help="Use MPI on experiments")

    parser.add_argument("--cache", default=False, action="store_true",
                        help="Disable O_DIRECT on experiments")

    args = parser.parse_args()

    class ZFPCompiler(GNUCompiler):
        def __init__(self, *c_args, **kwargs):

            super(ZFPCompiler, self).__init__(*c_args, **kwargs)

            #self.libraries.append("zfp")
            if args.cache:
                d_cache = "CACHE=1"
                self.defines.append(d_cache)
            d_ndisks = "NDISKS=%d" % args.disks
            d_rate = "RATE=%d" % args.rate

            self.defines.append(d_ndisks)
            self.defines.append(d_rate)

    compiler_registry['zfpcompile'] = ZFPCompiler
    configuration.add("compiler", "custom", list(compiler_registry), callback=lambda i: compiler_registry[i]())
    configuration['compiler'] = 'zfpcompile'

    to_disk = not args.ram
    run(nbpml=args.nbpml,
        space_order=args.space_order,
        kernel=args.kernel,
        filename='overthrust_3D_initial_model.h5',
        to_disk=to_disk,
        compression=args.compression,
        mpi=args.mpi)
