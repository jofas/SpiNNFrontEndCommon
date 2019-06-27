from six import add_metaclass
from spinn_utilities.abstract_base import (
    AbstractBase, abstractmethod)


@add_metaclass(AbstractBase)
class AbstractCanReset(object):
    """ Indicates an object that can be reset to time 0
    """

    @abstractmethod
    def reset(self):
        """ Reset the object
        """
