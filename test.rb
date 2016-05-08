require 'benchmark'


module TraceDemo
  class Frame
    def initialize tp
      @path = tp.path
      @method = tp.method_id
      @lineno = tp.lineno
      @binding = tp.binding
    end

    attr_reader :path, :lineno, :method

    if binding.respond_to?(:local_variables)
      def receiver
        @binding.receiver
      end

      def locals
        rv = {}
        @binding.local_variables.each do |var|
          rv[var] = @binding.local_variable_get(var)
        end
        rv
      end
    else
      def receiver
        @binding.eval('self')
      end

      def locals
        rv = {}
        @binding.eval('local_variables').each do |var|
          rv[var] = @binding.eval(var.to_s)
        end
        rv
      end
    end

    def class_name
      receiver.class.name
    end

    def inspect
      "#<Frame #{path}:#{lineno} in #{class_name}##{method} #{locals.inspect}>"
    end
  end

  class Stacktrace
    def initialize frames
      @frames = frames
    end

    attr_reader :frames

    def inspect
      "#<TraceDemo::Stacktrace frames=[#{@frames.map {|f| f.inspect}.join(', ')}]>"
    end
  end

  def self.make_tracepoint
    TracePoint.new(:call, :return, :raise) do |tp|
      evt = tp.event
      if evt == :call
        (Thread.current[:trace_demo_stack] ||= []) << Frame.new(tp)
      elsif evt == :return
        Thread.current[:trace_demo_stack].pop
      else
        exc = tp.raised_exception
        exc.instance_variable_set(
          :@__trace_stack, Thread.current[:trace_demo_stack].dup)
      end
    end
  end

  def self.get_stacktrace err
    Stacktrace.new(err.instance_variable_get(:@__trace_stack))
  end

  def self.capture_stack
    self.make_tracepoint.enable do
      begin
        yield
      rescue Exception => e
        stacktrace = TraceDemo.get_stacktrace(e)
        stacktrace.frames.each do |f|
          p f
        end
      end
    end
  end
end

def bar
  1/0
end

def foo a, b, pass=false
  x = "#{a} + #{b} = #{a + b}"
  bar unless pass
  x
end

def test
  TraceDemo.capture_stack do
    foo 1, 2
  end
end

def bench
  Benchmark.bmbm do |x|
    n = 200000

    x.report('exc:normal') {
      n.times do
        begin
          foo 1, 2
        rescue
        end
      end
    }

    x.report('exc:capture') {
      TraceDemo.make_tracepoint.enable do
        n.times do
          begin
            foo 1, 2
          rescue
          end
        end
      end
    }

    x.report('pass:normal') {
      n.times do
        foo 1, 2, true
      end
    }

    x.report('pass:capture') {
      TraceDemo.make_tracepoint.enable do
        n.times do
          begin
            foo 1, 2, true
          rescue
          end
        end
      end
    }
  end
end
