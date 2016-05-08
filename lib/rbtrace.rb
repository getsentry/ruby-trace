module RbTrace
  VERSION = "1.0"

  class Frame
    def initialize(path, lineno, method_id, binding)
      @path = path
      @method_id = method_id
      @lineno = lineno
      @binding = binding
    end

    attr_reader :path, :lineno, :method_id

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
      "#<Frame #{path}:#{lineno} in #{class_name}##{method_id} #{locals.inspect}>"
    end
  end

  class Stacktrace
    def initialize(frames)
      @frames = frames
    end

    attr_reader :frames

    def inspect
      "#<TraceDemo::Stacktrace frames=[#{@frames.map {|f| f.inspect}.join(', ')}]>"
    end
  end

  def self.get_stacktrace(exc)
    stack_state = exc.instance_variable_get(:@__rbtrace_state)
    if stack_state
      Stacktrace.new(stack_state.frames)
    end
  end

  def self.capture_stack
    self.make_tracepoint.enable do
      begin
        yield
      rescue Exception => e
        stacktrace = get_stacktrace(e)
        stacktrace.frames.each do |f|
          p f
        end
      end
    end
  end
end

require "rbtrace/rbtrace"
