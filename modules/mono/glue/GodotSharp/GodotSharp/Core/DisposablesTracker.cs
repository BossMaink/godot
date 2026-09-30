using System;
using System.Collections.Concurrent;
using System.Runtime.InteropServices;
using Godot.NativeInterop;

#nullable enable

namespace Godot
{
    internal static class DisposablesTracker
    {
        [UnmanagedCallersOnly]
        internal static void OnGodotShuttingDown()
        {
            try
            {
                OnGodotShuttingDownImpl();
            }
            catch (Exception e)
            {
                ExceptionUtils.LogException(e);
            }
        }

        private static void OnGodotShuttingDownImpl()
        {
            bool isStdoutVerbose;

            try
            {
                isStdoutVerbose = OS.IsStdOutVerbose();
            }
            catch (ObjectDisposedException)
            {
                // OS singleton already disposed. Maybe OnUnloading was called twice.
                isStdoutVerbose = false;
            }

            if (isStdoutVerbose)
                GD.Print("Unloading: Disposing tracked instances...");

            // Dispose Godot Objects first, and only then dispose other disposables
            // like StringName, NodePath, Godot.Collections.Array/Dictionary, etc.
            // The Godot Object Dispose() method may need any of the later instances.

            // Short weak references cannot expose targets already queued for
            // finalization. Finish those destructors while native C# bindings
            // still exist, then explicitly dispose surviving wrappers. Disposal
            // can make more wrappers unreachable, so drain to an empty registry.
            do
            {
                GC.Collect();
                GC.WaitForPendingFinalizers();
                foreach (WeakReference<GodotObject> item in GodotObjectInstances.Keys)
                {
                    if (item.TryGetTarget(out GodotObject? self))
                        self.Dispose();
                }
    
                foreach (WeakReference<IDisposable> item in OtherInstances.Keys)
                {
                    if (item.TryGetTarget(out IDisposable? self))
                        self.Dispose();
                }
    

            } while (!GodotObjectInstances.IsEmpty || !OtherInstances.IsEmpty);
            GC.WaitForPendingFinalizers();

            if (isStdoutVerbose)
                GD.Print("Unloading: Finished disposing tracked instances.");
        }

        private static ConcurrentDictionary<WeakReference<GodotObject>, byte> GodotObjectInstances { get; } =
            new();

        private static ConcurrentDictionary<WeakReference<IDisposable>, byte> OtherInstances { get; } =
            new();

        public static WeakReference<GodotObject> RegisterGodotObject(GodotObject godotObject)
        {
            var weakReferenceToSelf = new WeakReference<GodotObject>(godotObject);
            GodotObjectInstances.TryAdd(weakReferenceToSelf, 0);
            return weakReferenceToSelf;
        }

        public static WeakReference<IDisposable> RegisterDisposable(IDisposable disposable)
        {
            var weakReferenceToSelf = new WeakReference<IDisposable>(disposable);
            OtherInstances.TryAdd(weakReferenceToSelf, 0);
            return weakReferenceToSelf;
        }

        public static void UnregisterGodotObject(GodotObject godotObject, WeakReference<GodotObject> weakReferenceToSelf)
        {
            if (!GodotObjectInstances.TryRemove(weakReferenceToSelf, out _))
                throw new ArgumentException("Godot Object not registered.", nameof(weakReferenceToSelf));
        }

        public static void UnregisterDisposable(WeakReference<IDisposable> weakReference)
        {
            if (!OtherInstances.TryRemove(weakReference, out _))
                throw new ArgumentException("Disposable not registered.", nameof(weakReference));
        }
    }
}
