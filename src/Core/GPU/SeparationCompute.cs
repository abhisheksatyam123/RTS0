// GPU compute for separation — RenderingDevice path, CPU fallback mirrors GameSim
// Mirrors GameSim.SeparationPass (Parallel.For) but on GPU when count > 512 and Vulkan available.
using System;
using Godot;
using Rts0.Core;

namespace Rts0.Core.Gpu;

public sealed class SeparationCompute : IDisposable
{
    private RenderingDevice _rd;
    private Rid _shader;
    private Rid _pipeline;
    private bool _available;

    public bool IsAvailable => _available;

    public SeparationCompute()
    {
        try
        {
            _rd = RenderingServer.GetRenderingDevice();
            _available = _rd != null;
            if (!_available) return;

            var shaderFile = GD.Load<RDShaderFile>("res://shaders/compute/separation.glsl");
            if (shaderFile == null) { _available = false; return; }
            var spirv = shaderFile.GetSpirV();
            _shader = _rd.ShaderCreateFromSpirV(spirv);
            _pipeline = _rd.ComputePipelineCreate(_shader);
        }
        catch
        {
            _available = false;
        }
    }

    // Returns forces or null if GPU not available (caller falls back to CPU)
    public Vector2[] TryCompute(ReadOnlySpan<Vector3> positions, float minDist, float strength = 0.25f)
    {
        if (!_available || _rd == null || positions.Length < 512) return null;
        try
        {
            int count = positions.Length;
            var posData = new byte[count * 8];
            for (int i = 0; i < count; i++)
            {
                var v = positions[i];
                BitConverter.GetBytes(v.X).CopyTo(posData, i * 8);
                BitConverter.GetBytes(v.Z).CopyTo(posData, i * 8 + 4);
            }
            var forceData = new byte[count * 8];
            // create buffers
            var posBuf = _rd.StorageBufferCreate((uint)posData.Length, posData);
            var forceBuf = _rd.StorageBufferCreate((uint)forceData.Length);
            var uniformPos = new RDUniform { UniformType = RenderingDevice.UniformType.StorageBuffer, Binding = 0 };
            uniformPos.AddId(posBuf);
            var uniformForce = new RDUniform { UniformType = RenderingDevice.UniformType.StorageBuffer, Binding = 1 };
            uniformForce.AddId(forceBuf);
            var set = _rd.UniformSetCreate(new[] { uniformPos, uniformForce }, _shader, 0);
            var push = new byte[16];
            BitConverter.GetBytes((uint)count).CopyTo(push, 0);
            BitConverter.GetBytes(0f).CopyTo(push, 4); // cellSize placeholder
            BitConverter.GetBytes(minDist).CopyTo(push, 8);
            BitConverter.GetBytes(strength).CopyTo(push, 12);

            var list = _rd.ComputeListBegin();
            _rd.ComputeListBindComputePipeline(list, _pipeline);
            _rd.ComputeListBindUniformSet(list, set, 0);
            _rd.ComputeListSetPushConstant(list, push, (uint)push.Length);
            _rd.ComputeListDispatch(list, (uint)((count + 63) / 64), 1, 1);
            _rd.ComputeListEnd();
            _rd.Submit();
            _rd.Sync();

            var outBytes = _rd.BufferGetData(forceBuf);
            var forces = new Vector2[count];
            for (int i = 0; i < count; i++)
            {
                float fx = BitConverter.ToSingle(outBytes, i * 8);
                float fz = BitConverter.ToSingle(outBytes, i * 8 + 4);
                forces[i] = new Vector2(fx, fz);
            }
            _rd.FreeRid(posBuf);
            _rd.FreeRid(forceBuf);
            return forces;
        }
        catch
        {
            return null;
        }
    }

    public void Dispose()
    {
        if (_rd != null && _shader.IsValid) _rd.FreeRid(_shader);
    }
}
